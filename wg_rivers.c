#include "wg_rivers.h"

#include <assert.h>
#include <math.h>

#include "fmath.h"

/* Write v into derived slot `slot` (RLK_WG_NONE = skip). */
static void
set_derived(rlk_wg_ctx_t *ctx, uint8_t slot, float v) {
    if (slot == RLK_WG_NONE)
        return;
    const int idx = ctx->wg->nsampled + slot;
    assert(idx < ctx->wg->nsampled + ctx->wg->nderived);
    ctx->field[idx] = v;
}

/* River mask and valley carve from the drain flowline SDF at (wxf, wyf):
   full mask in the channel, fading over bank_fade tiles past the bank;
   quadratic valley profile with reach = valley_mult * cellsize. */
static void
river_sdf(const rlk_wg_rivers_cfg_t *r, float wxf, float wyf,
          float bank_fade, float *mask, float *carve) {
    const float sdf = rlk_drain_flowline_sdf(r->drain, (int)floorf(wxf), (int)floorf(wyf),
                                             r->acc_min);
    if (sdf >= RLK_DRAIN_FAR)
        return;
    *mask = rlk_clampf(1.0f - sdf / bank_fade, 0.0f, 1.0f);
    const float reach = r->valley_mult * (float)r->elev->lattice.cellsize;
    const float vall = rlk_clampf(1.0f - sdf / reach, 0.0f, 1.0f);
    *carve = r->carve * vall * vall;
}

/* Creeks: noise level-line streams added on top of the drainage network. */
static void
creek_add(const rlk_wg_t *wg, const rlk_wg_rivers_cfg_t *r, float wxf, float wyf,
          float *mask, float *carve) {
    const float cv = rlk_wg_sample_field(wg, r->f_creek, wxf, wyf);
    const float cx = rlk_wg_sample_field(wg, r->f_creek, wxf + 1.0f, wyf) - cv;
    const float cy = rlk_wg_sample_field(wg, r->f_creek, wxf, wyf + 1.0f) - cv;
    float g = sqrtf(cx * cx + cy * cy);
    if (g < 0.004f) g = 0.004f;
    const float dist = fabsf(cv - 0.5f) / g;
    const float m = 1.0f - dist / r->creek_width;
    if (m > *mask)
        *mask = m;
    const float vall = 1.0f - dist / (r->valley_mult * r->creek_width);
    if (vall > 0.0f) {
        const float cc = r->creek_carve * vall * vall;
        if (cc > *carve)
            *carve = cc;
    }
}

/* Rivers do not flow under the sea: zero mask and carve where the coarse
   elevation is below sea level, at the original tile position. */
static void
sea_clip(const rlk_wg_rivers_cfg_t *r, int wx, int wy, float *mask, float *carve) {
    const float e = rlk_grid_get(r->elev, (float)wx + 0.5f, (float)wy + 0.5f);
    if (e < r->sea) {
        *mask = 0.0f;
        *carve = 0.0f;
    }
}

/* Lake overlay at the tile centre (a lake is a property of the terrain, not
   of the meander): smooth threshold at lake_min_depth, combined as max. */
static void
lake_add(const rlk_wg_rivers_cfg_t *r, int wx, int wy, float *mask) {
    if (!r->lake_depth || r->lake_min_depth <= 0.0f)
        return;
    const float depth = rlk_grid_get(r->lake_depth, (float)wx + 0.5f, (float)wy + 0.5f);
    if (depth < r->lake_min_depth)
        return;
    const float lm = (depth - 0.5f * r->lake_min_depth) / (0.5f * r->lake_min_depth);
    const float lc = rlk_clampf(lm, 0.0f, 1.0f);
    if (lc > *mask)
        *mask = lc;
}

static void
rivers_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_rivers_cfg_t *r = (const rlk_wg_rivers_cfg_t *)data;
    const rlk_wg_t *wg = ctx->wg;

    /* Slot bounds are checked here (the constructor does not know the wg). */
    assert(r->slot_mask == RLK_WG_NONE || (int)r->slot_mask < wg->nderived);
    assert(r->slot_carve == RLK_WG_NONE || (int)r->slot_carve < wg->nderived);

    /* Log-width law: the effective bank fade grows with the normalized
       log10(acc) read from the drain (acc <= w*h by construction, so the
       lattice size is the map-wide maximum); a larger fade widens the mask
       for the same SDF distance. Off when acc_log_gain <= 0. */
    float bank_fade = r->bank_fade;
    if (r->acc_log_gain > 0.0f) {
        const int c = rlk_lattice_index(&r->drain->lattice, ctx->wx, ctx->wy);
        float acc_log = 0.0f;
        if (c >= 0) {
            const float norm = log10f((float)(r->drain->lattice.w * r->drain->lattice.h) + 1.0f);
            acc_log = log10f((float)r->drain->acc[(size_t)c] + 1.0f) / norm;
        }
        bank_fade *= 1.0f + r->acc_log_gain * acc_log;
    }

    /* Tile centre in world coords, displaced by optional meanders (quantized
       to whole tiles below: the SDF is tile-granular). */
    float wxf = (float)ctx->wx + 0.5f;
    float wyf = (float)ctx->wy + 0.5f;
    if (r->f_wig_x != RLK_WG_NONE && r->f_wig_y != RLK_WG_NONE) {
        wxf += (ctx->field[r->f_wig_x] - 0.5f) * 2.0f * r->wig_tiles;
        wyf += (ctx->field[r->f_wig_y] - 0.5f) * 2.0f * r->wig_tiles;
    }

    float mask = 0.0f;
    float carve = 0.0f;
    river_sdf(r, wxf, wyf, bank_fade, &mask, &carve);
    if (r->f_creek != RLK_WG_NONE)
        creek_add(wg, r, wxf, wyf, &mask, &carve);
    sea_clip(r, ctx->wx, ctx->wy, &mask, &carve);
    lake_add(r, ctx->wx, ctx->wy, &mask);

    set_derived(ctx, r->slot_mask, rlk_clampf(mask, 0.0f, 1.0f));
    set_derived(ctx, r->slot_carve, carve);
}

rlk_wg_stage_t
rlk_wg_rivers(rlk_wg_rivers_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->drain && cfg->elev);
    assert(rlk_lattice_same(&cfg->drain->lattice, &cfg->elev->lattice));
    /* Resolve zero defaults in place: acc_min 0->1, bank_fade<=0->1.0,
       valley_mult<1->1.0; wig_tiles<=0 disables meanders, creek_width<=0
       disables creeks. */
    if (cfg->acc_min == 0)
        cfg->acc_min = 1;
    if (cfg->bank_fade <= 0.0f)
        cfg->bank_fade = 1.0f;
    if (cfg->valley_mult < 1.0f)
        cfg->valley_mult = 1.0f;
    if (cfg->wig_tiles <= 0.0f) {
        cfg->f_wig_x = RLK_WG_NONE;
        cfg->f_wig_y = RLK_WG_NONE;
    }
    if (cfg->creek_width <= 0.0f)
        cfg->f_creek = RLK_WG_NONE;
    return (rlk_wg_stage_t){ .fn = rivers_stage, .data = cfg };
}
