#include "wg.h"
#include <assert.h>
#include <math.h>

#include "hash.h"

/* Coordinate naming: x,y chunk-local; wx,wy world tile; origin_x,origin_y
 * rect origin (wx == origin_x + x); wxf,wyf float world position
 * (wxf == (float)wx). */

/* --- SETUP --------------------------------------------------------------- */

/* Safety checks only: prevent crashes (buffer overflow, NULL deref, div by zero).
   Logic errors (hi <= lo, freq <= 0, etc.) are the user's responsibility. */
static void
assert_wg(const rlk_wg_t *wg) {
    assert(wg);
    assert(wg->nsampled >= 1 && wg->nsampled <= RLK_WG_MAX_FIELDS);
    assert(wg->nderived >= 0 && wg->nsampled + wg->nderived <= RLK_WG_MAX_FIELDS);
    assert(wg->nstages >= 0 && wg->nstages <= RLK_WG_MAX_STAGES);
    for (int i = 0; i < wg->nstages; i++)
        assert(wg->stages[i].fn);
    assert(wg->naxes >= 1 && wg->naxes <= RLK_WG_MAX_AXES);
    assert(wg->nzones >= 1 && wg->nzones <= RLK_WG_MAX_ZONES);
    assert(wg->depth_scale > 0.0f && wg->transition_width > 0.0f);

    const int nslots = wg->nsampled + wg->nderived;
    for (int a = 0; a < wg->naxes; a++) {
        const rlk_wg_axis_t *ax = &wg->axes[a];
        assert(ax->field < nslots);
        assert(ax->jitter_amp == 0.0f || ax->jitter_field < nslots);
    }

    for (int i = 0; i < wg->nzones; i++)
        assert(wg->zones[i].write);
}

rlk_err_t
rlk_wg_add(rlk_wg_t *wg, rlk_wg_stage_t stage) {
    assert(wg && stage.fn);
    if (wg->nstages >= RLK_WG_MAX_STAGES)
        return RLK_E_INVALID;
    wg->stages[wg->nstages] = stage;
    wg->nstages++;
    return RLK_OK;
}

/* Expand RLK_WG_SLOTS_ALL to all n bits; reject masks with bits >= n. */
static rlk_err_t
slot_mask(uint32_t m, int n, uint32_t *out) {
    if (m == RLK_WG_SLOTS_ALL) {
        *out = n >= 32 ? ~0u : ((1u << n) - 1u);
        return RLK_OK;
    }
    if (n < 32 && (m >> n) != 0u)
        return RLK_E_INVALID;
    *out = m;
    return RLK_OK;
}

/* Validate the slot declarations, in registration order: a derived slot read
   by a stage must have been written by an earlier stage (sampled slots are
   sources, always readable). The axes are read by classify after all stages:
   an axis on a derived slot requires that slot to be written by some stage. */
static rlk_err_t
check_stage_slots(const rlk_wg_t *wg) {
    uint32_t written = 0;   /* derived bits written so far */
    for (int i = 0; i < wg->nstages; i++) {
        const rlk_wg_stage_t *st = &wg->stages[i];
        uint32_t rs, rd, ws, wd;
        if (slot_mask(st->reads.sampled, wg->nsampled, &rs) != RLK_OK ||
            slot_mask(st->reads.derived, wg->nderived, &rd) != RLK_OK ||
            slot_mask(st->writes.sampled, wg->nsampled, &ws) != RLK_OK ||
            slot_mask(st->writes.derived, wg->nderived, &wd) != RLK_OK)
            return RLK_E_INVALID;
        if (rd & ~written)
            return RLK_E_INVALID;
        written |= wd;
    }
    for (int a = 0; a < wg->naxes; a++) {
        const rlk_wg_axis_t *ax = &wg->axes[a];
        if (ax->field >= wg->nsampled &&
            ((written >> (ax->field - wg->nsampled)) & 1u) == 0u)
            return RLK_E_INVALID;
        if (ax->jitter_amp > 0.0f && ax->jitter_field >= wg->nsampled &&
            ((written >> (ax->jitter_field - wg->nsampled)) & 1u) == 0u)
            return RLK_E_INVALID;
    }
    return RLK_OK;
}

/* Resolve zero defaults (octaves->3, gain->0.5), validate, and seed noise. */
rlk_err_t
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
    assert_wg(wg);

    /* Slot declaration checks (stage order, axes). */
    rlk_err_t e = check_stage_slots(wg);
    if (e != RLK_OK)
        return e;

    /* Seed noise generators: world seed + index + field seed. */
    for (int i = 0; i < wg->nsampled; i++) {
        uint64_t s = rlk_hash_sub(wg->seed, 0xF1E1D000ULL + (uint64_t)i);
        rlk_noise_seed(&wg->noise[i], rlk_hash_sub(s, wg->fields[i].seed));
    }
    return RLK_OK;
}

/* --- FIELDS -------------------------------------------------------------- */

/* Sample field f at float world position (x, y). Public: stages reuse it. */
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

/* Classify tile into zone/zone2 with depth/edge. */
static inline void
classify(const rlk_wg_t *wg, rlk_wg_ctx_t *ctx) {
    assert(wg && ctx && wg->nzones > 0);
    const int naxes = wg->naxes;
    float v[RLK_WG_MAX_AXES];

    for (int a = 0; a < naxes; a++) {
        const rlk_wg_axis_t *ax = &wg->axes[a];
        v[a] = ctx->field[ax->field];
        if (ax->jitter_amp > 0.0f)
            v[a] += (ctx->field[ax->jitter_field] - 0.5f) * 2.0f * ax->jitter_amp;
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
    ctx->depth = rlk_clampf(mb / wg->depth_scale, 0.0f, 1.0f);
    ctx->edge = second >= 0
        ? rlk_clampf(1.0f - (mb - ms) / wg->transition_width, 0.0f, 1.0f)
        : 0.0f;
}

/* --- PER-TILE PIPELINE --------------------------------------------------- */

/* Evaluate one tile: fields -> stages -> classify. Fills ctx.
   A tile outside every zone box gets the nearest zone (depth = 0). */
static void
eval(const rlk_wg_t *wg, int wx, int wy, int x, int y,
     rlk_wg_ctx_t *ctx) {
    assert(wg && ctx);

    *ctx = (rlk_wg_ctx_t){
        .wg = wg,
        .wx = wx,
        .wy = wy,
        .x  = x,
        .y  = y,
    };

    /* FIELDS */
    const float wxf = (float)wx, wyf = (float)wy;
    for (int i = 0; i < wg->nsampled; i++)
        ctx->field[i] = rlk_wg_sample_field(wg, i, wxf, wyf);

    /* STAGES */
    for (int i = 0; i < wg->nstages; i++)
        wg->stages[i].fn(ctx, wg->stages[i].data);

    /* CLASSIFY */
    classify(wg, ctx);
}

/* --- DRIVER -------------------------------------------------------------- */

/* Fill tilemap tm with tiles for the world-space rect starting at
 * (origin_x, origin_y). Rect dimensions are tm->w x tm->h tiles. */
void
rlk_wg_fill(const rlk_wg_t *wg, int origin_x, int origin_y, rlk_tilemap_t *tm) {
    assert(wg && tm);
    assert(tm->w > 0 && tm->h > 0);

    for (int y = 0; y < tm->h; y++) {
        for (int x = 0; x < tm->w; x++) {
            rlk_wg_ctx_t ctx;
            eval(wg, origin_x + x, origin_y + y, x, y, &ctx);

            /* WRITE */
            const rlk_wg_zone_t *z = &wg->zones[ctx.zone];
            rlk_tile_t *tile = rlk_tilemap_get(tm, tm->origin_x + x, tm->origin_y + y);
            assert(tile);
            /* Reset tile, set biome to zone id, then the zone writer fills type/flags/variant. */
            *tile = (rlk_tile_t){.biome = ctx.zone};
            z->write(&ctx, z->data, tile);
        }
    }
}
