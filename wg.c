#include "wg.h"
#include <assert.h>
#include <math.h>

#include "hash.h"

/* --- SETUP --------------------------------------------------------------- */

rlk_err_t
rlk_wg_add(rlk_wg_t *wg, rlk_wg_stage_t stage) {
    assert(wg && stage.fn);
    if (wg->nstages >= RLK_WG_MAX_STAGES)
        return RLK_E_INVALID;
    wg->stages[wg->nstages] = stage;
    wg->nstages++;
    return RLK_OK;
}

static void
assert_wg_cfg(const rlk_wg_t *wg) {
    assert(wg);
    assert(wg->nsampled >= 1 && wg->nsampled <= RLK_WG_MAX_FIELDS);
    assert(wg->nderived >= 0 && wg->nsampled + wg->nderived <= RLK_WG_MAX_FIELDS);
    assert(wg->nstages >= 0 && wg->nstages <= RLK_WG_MAX_STAGES);
    for (int i = 0; i < wg->nstages; i++)
        assert(wg->stages[i].fn);
    assert(wg->naxes >= 1 && wg->naxes <= RLK_WG_MAX_AXES);
    assert(wg->nzones >= 1 && wg->nzones <= RLK_WG_MAX_ZONES);
    assert(wg->inside_scale > 0.0f && wg->border_width > 0.0f);

    for (int i = 0; i < wg->nsampled; i++)
        assert(wg->fields[i].freq > 0.0f);

    const int nslots = wg->nsampled + wg->nderived;
    for (int a = 0; a < wg->naxes; a++) {
        const rlk_wg_axis_t *ax = &wg->axes[a];
        assert(ax->slot < nslots);
        assert(ax->jitter_amp == 0.0f || ax->jitter_slot < nslots);
    }

    for (int i = 0; i < wg->nzones; i++) {
        const rlk_wg_zone_t *z = &wg->zones[i];
        assert(z->write);
        for (int a = 0; a < wg->naxes; a++)
            assert(z->lo[a] < z->hi[a]);
    }
}

void
rlk_wg_init(rlk_wg_t *wg) {
    assert(wg);

    /* Resolve zero defaults. */
    for (int i = 0; i < wg->nsampled; i++) {
        rlk_wg_field_t *f = &wg->fields[i];
        if (f->octaves == 0)
            f->octaves = 3;
        if (f->gain == 0.0f)
            f->gain = 0.5f;
    }

    /* Safety checks. */
    assert_wg_cfg(wg);

    /* Seed noise generators: world seed + index + field seed. */
    for (int i = 0; i < wg->nsampled; i++) {
        uint64_t s = rlk_hash_sub(wg->seed, 0xF1E1D000ULL + (uint64_t)i);
        wg->noise[i].seed = rlk_hash_sub(s, wg->fields[i].salt);
    }
}

/* --- FIELDS -------------------------------------------------------------- */

float
rlk_wg_sample_field(const rlk_wg_t *wg, int idx, float x, float y) {
    assert(wg && idx >= 0 && idx < wg->nsampled);
    const rlk_noise_t *n = &wg->noise[idx];
    const rlk_wg_field_t *f = &wg->fields[idx];
    assert(f->freq > 0.0f && f->gain > 0.0f);
    float v = rlk_noise_fbm(n, x, y, f->octaves, f->freq, f->gain);
    if (f->kind == RLK_WG_RIDGE)
        v = 1.0f - fabsf(2.0f * v - 1.0f);
    return v;
}

/* --- CLASSIFY ------------------------------------------------------------ */

/* Signed distance to zone bounds: > 0 inside, < 0 outside. */
static inline float
zone_margin(const rlk_wg_zone_t *z, const float v[RLK_WG_MAX_AXES], int naxes) {
    float m = fminf(v[0] - z->lo[0], z->hi[0] - v[0]);
    for (int a = 1; a < naxes; a++)
        m = fminf(m, fminf(v[a] - z->lo[a], z->hi[a] - v[a]));
    return m;
}

/* Classify tile into zone/zone2 with inside/border. */
static inline void
classify(const rlk_wg_t *wg, rlk_wg_ctx_t *ctx) {
    assert(wg && ctx && wg->nzones > 0);
    const int naxes = wg->naxes;
    float v[RLK_WG_MAX_AXES];

    for (int a = 0; a < naxes; a++) {
        const rlk_wg_axis_t *ax = &wg->axes[a];
        v[a] = ctx->field[ax->slot];
        if (ax->jitter_amp > 0.0f)
            v[a] += (ctx->field[ax->jitter_slot] - 0.5f) * 2.0f * ax->jitter_amp;
    }

    int best = -1, second = -1;
    float mb = -INFINITY, ms = -INFINITY;

    for (int i = 0; i < wg->nzones; i++) {
        float m = zone_margin(&wg->zones[i], v, naxes);
        if (m > mb) {
            second = best;
            ms = mb;
            best = i;
            mb = m;
        } else if (m > ms) {
            second = i;
            ms = m;
        }
    }

    ctx->zone = (uint8_t)best;
    ctx->zone2 = second >= 0 ? (uint8_t)second : ctx->zone;
    ctx->inside = rlk_clampf(mb / wg->inside_scale, 0.0f, 1.0f);
    ctx->border = second >= 0
        ? rlk_clampf(1.0f - (mb - ms) / wg->border_width, 0.0f, 1.0f)
        : 0.0f;
}

/* --- PIPELINE&FILL ------------------------------------------------------- */

static void
eval_tile(const rlk_wg_t *wg, int wx, int wy, int x, int y, rlk_wg_ctx_t *ctx) {
    assert(wg && ctx);

    *ctx = (rlk_wg_ctx_t){
        .wg = wg,
        .wx = wx,
        .wy = wy,
        .x  = x,
        .y  = y,
    };

    /* FIELDS */
    const float wxf = (float)wx + 0.5f, wyf = (float)wy + 0.5f;
    for (int i = 0; i < wg->nsampled; i++)
        ctx->field[i] = rlk_wg_sample_field(wg, i, wxf, wyf);

    /* STAGES */
    for (int i = 0; i < wg->nstages; i++)
        wg->stages[i].fn(ctx, wg->stages[i].data);

    /* CLASSIFY */
    classify(wg, ctx);
}

static void
write_tile(const rlk_wg_ctx_t *ctx, void *user) {
    assert(ctx && user);
    rlk_tilemap_t *tm = (rlk_tilemap_t *)user;
    rlk_tile_t *tile = rlk_tilemap_get(tm, ctx->wx, ctx->wy);
    assert(tile);
    /* Reset the tile, store the zone index, then the zone writer fills the rest. */
    *tile = (rlk_tile_t){.biome = ctx->zone};
    const rlk_wg_zone_t *z = &ctx->wg->zones[ctx->zone];
    z->write(ctx, z->data, tile);
}

static void
run_pipeline(const rlk_wg_t *wg, int origin_x, int origin_y, int w, int h,
             rlk_tilemap_t *tm) {
    assert(wg && tm);
    assert(w > 0 && h > 0);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            rlk_wg_ctx_t ctx;
            eval_tile(wg, origin_x + x, origin_y + y, x, y, &ctx);
            write_tile(&ctx, tm);
        }
    }
}

void
rlk_wg_fill(const rlk_wg_t *wg, int origin_x, int origin_y, rlk_tilemap_t *tm) {
    assert(wg && tm);
    assert(tm->w > 0 && tm->h > 0);
    assert(origin_x == tm->origin_x && origin_y == tm->origin_y);
    run_pipeline(wg, origin_x, origin_y, tm->w, tm->h, tm);
}
