#include "wg_nf_rivers.h"

#include <assert.h>
#include <math.h>

#include "fmath.h"

/* Resolve zero defaults: acc_min 0->1, bank_fade<=0->1.0, valley_mult<1->1.0,
   wig_tiles<=0 disables meanders, creek_width<=0 disables creeks. */
static void
rivers_resolve(rlk_wg_nf_rivers_cfg_t *r) {
    assert(r->drain && r->elev);
    assert(rlk_lattice_same(&r->drain->lattice, &r->elev->lattice));
    if (r->acc_min == 0)
        r->acc_min = 1;
    if (r->bank_fade <= 0.0f)
        r->bank_fade = 1.0f;
    if (r->valley_mult < 1.0f)
        r->valley_mult = 1.0f;
    /* Meanders and creeks are opt-in: disable if amplitude is zero */
    if (r->wig_tiles <= 0.0f) {
        r->f_wig_x = RLK_WG_NF_RIVERS_NONE;
        r->f_wig_y = RLK_WG_NF_RIVERS_NONE;
    }
    if (r->creek_width <= 0.0f)
        r->f_creek = RLK_WG_NF_RIVERS_NONE;
}

static void
rivers_stage(rlk_wg_nf_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    rlk_wg_nf_rivers_cfg_t rc = *(const rlk_wg_nf_rivers_cfg_t *)data;
    rivers_resolve(&rc);
    const rlk_wg_nf_rivers_cfg_t *r = &rc;
    const rlk_wg_nf_t *wg = ctx->wg;

    /* Slots are validated against the wg's derived field count here
       (the constructor does not know the wg). */
    assert(r->slot_mask == RLK_WG_NF_RIVERS_NONE || (int)r->slot_mask < wg->nderived);
    assert(r->slot_carve == RLK_WG_NF_RIVERS_NONE || (int)r->slot_carve < wg->nderived);

    const float cell = (float)r->elev->lattice.cellsize;

    /* Tile centre in world coords, displaced by optional meanders */
    float px = (float)ctx->wx + 0.5f;
    float py = (float)ctx->wy + 0.5f;
    if (r->f_wig_x != RLK_WG_NF_RIVERS_NONE && r->f_wig_y != RLK_WG_NF_RIVERS_NONE) {
        px += (ctx->field[r->f_wig_x] - 0.5f) * 2.0f * r->wig_tiles;
        py += (ctx->field[r->f_wig_y] - 0.5f) * 2.0f * r->wig_tiles;
    }

    /* River network as a signed distance field (constant cost per tile).
       Meanders are quantized to whole tiles: the sdf is tile-granular. */
    float mask = 0.0f;
    float carve = 0.0f;
    const float d = rlk_drain_river_sdf(r->drain, (int)floorf(px), (int)floorf(py),
                                        r->acc_min);
    if (d < RLK_DRAIN_FAR) {
        /* Water: full mask in the channel, fading over bank_fade tiles. */
        mask = rlk_clampf(1.0f - d / r->bank_fade, 0.0f, 1.0f);
        /* Valley: quadratic profile, reach = valley_mult * cellsize. */
        const float reach = r->valley_mult * cell;
        const float vall = rlk_clampf(1.0f - d / reach, 0.0f, 1.0f);
        carve = r->carve * vall * vall;
    }

    /* Creeks: noise level-line streams on top of the drainage network */
    if (r->f_creek != RLK_WG_NF_RIVERS_NONE && r->creek_width > 0.0f) {
        const float cv = rlk_wg_nf_sample_field(wg, r->f_creek, px, py);
        const float cx = rlk_wg_nf_sample_field(wg, r->f_creek, px + 1.0f, py) - cv;
        const float cy = rlk_wg_nf_sample_field(wg, r->f_creek, px, py + 1.0f) - cv;
        float g = sqrtf(cx * cx + cy * cy);
        if (g < 0.004f) g = 0.004f;
        const float dist = fabsf(cv - 0.5f) / g;
        const float m = 1.0f - dist / r->creek_width;
        if (m > mask)
            mask = m > 0.0f ? m : 0.0f;
        const float vall = 1.0f - dist / (r->valley_mult * r->creek_width);
        if (vall > 0.0f) {
            const float cc = r->creek_carve * vall * vall;
            if (cc > carve)
                carve = cc;
        }
    }

    /* Rivers do not flow under the sea: zero the mask where the coarse
       elevation is below sea level. Uses the original tile position (not the
       wiggle-displaced one). Lake overlay below is exempt (inland depressions). */
    {
        const float e_coarse = rlk_grid_sample(r->elev, (float)ctx->wx + 0.5f, (float)ctx->wy + 0.5f);
        if (e_coarse < r->sea) {
            mask = 0.0f;
            carve = 0.0f;
        }
    }

    /* Lake overlay: bilinear sample of the lake-depth grid.
       Uses the original tile position (not the wiggle-displaced one):
       a lake is a property of the terrain, not of the meander. */
    if (r->lake_depth && r->lake_min_depth > 0.0f) {
        const float depth = rlk_grid_sample(r->lake_depth, (float)ctx->wx + 0.5f, (float)ctx->wy + 0.5f);
        if (depth >= r->lake_min_depth) {
            /* Smooth threshold */
            const float lm = (depth - 0.5f * r->lake_min_depth) / (0.5f * r->lake_min_depth);
            const float lc = lm < 0.0f ? 0.0f : (lm > 1.0f ? 1.0f : lm);
            if (lc > mask)
                mask = lc;
        }
    }

    /* Write outputs */
    if (r->slot_mask != RLK_WG_NF_RIVERS_NONE) {
        const int idx = wg->nsampled + r->slot_mask;
        assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);
        ctx->field[idx] = mask < 0.0f ? 0.0f : (mask > 1.0f ? 1.0f : mask);
    }
    if (r->slot_carve != RLK_WG_NF_RIVERS_NONE) {
        const int idx = wg->nsampled + r->slot_carve;
        assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);
        ctx->field[idx] = carve;
    }
}

rlk_wg_nf_stage_t
rlk_wg_nf_rivers(const rlk_wg_nf_rivers_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->drain && cfg->elev);
    assert(rlk_lattice_same(&cfg->drain->lattice, &cfg->elev->lattice));
    return (rlk_wg_nf_stage_t){ .fn = rivers_stage, .data = cfg };
}
