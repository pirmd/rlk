#include "wg_relief.h"

#include <assert.h>
#include <math.h>

#include "fmath.h"

/* Smoothstep between lo and hi (t in [0,1] mapped to [lo,hi]). */
static inline float
smoothstep_between(float lo, float hi, float t) {
    if (t <= lo) return 0.0f;
    if (t >= hi) return 1.0f;
    float u = (t - lo) / (hi - lo);
    return u * u * (3.0f - 2.0f * u);
}

/* Fjord carve at (wxf, wyf): straight, deep valley near cold coasts. */
static float
fjord_carve(const rlk_wg_t *wg, const rlk_wg_relief_cfg_t *r,
            float wxf, float wyf, float cont, float temp) {
    if (r->f_fjord == RLK_WG_NONE)
        return 0.0f;
    if (!(cont > r->sea))
        return 0.0f;

    /* Coast factor: active near the shoreline */
    const float coast = 1.0f - smoothstep_between(0.0f, r->fjord_reach, cont - r->sea);
    if (coast <= 0.0f)
        return 0.0f;

    /* Cold factor: only in cold regions */
    float cold = 1.0f;
    if (r->f_temp != RLK_WG_NONE)
        cold = 1.0f - smoothstep_between(r->fjord_cold0, r->fjord_cold1, temp);
    if (cold <= 0.0f)
        return 0.0f;

    /* Rotate and stretch coordinates onto the fjord axis */
    const float u = wxf * r->fjord_dir[0] + wyf * r->fjord_dir[1];
    const float v = -wxf * r->fjord_dir[1] + wyf * r->fjord_dir[0];

    const float fn = rlk_wg_sample_field(wg, r->f_fjord, u * r->fjord_stretch, v);
    const float fr = 1.0f - fabsf(2.0f * fn - 1.0f);
    const float vv = rlk_clampf((fr - (1.0f - r->fjord_width)) / r->fjord_width, 0.0f, 1.0f);
    const float steeper = vv * r->fjord_steep;
    const float profile = steeper > 1.0f
        ? 1.0f
        : steeper * steeper * (3.0f - 2.0f * steeper);

    return r->fjord_depth * coast * cold * profile;
}

static void
relief_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_relief_cfg_t *r = (const rlk_wg_relief_cfg_t *)data;
    const rlk_wg_t *wg = ctx->wg;

    const float wxf = (float)ctx->wx + 0.5f;   /* tile centre */
    const float wyf = (float)ctx->wy + 0.5f;

    /* Continent value (sampled at the original position by the field pass) */
    const float cont = ctx->field[r->f_cont];

    /* Domain warp: displace position using warp fields */
    float qx = wxf, qy = wyf;
    if (r->f_warp_x != RLK_WG_NONE && r->f_warp_y != RLK_WG_NONE) {
        const float vx = ctx->field[r->f_warp_x];
        const float vy = ctx->field[r->f_warp_y];
        qx += (vx - 0.5f) * 2.0f * r->warp;
        qy += (vy - 0.5f) * 2.0f * r->warp;
    }

    /* Base elevation: cont + hills + mountains */
    float elev = cont;
    if (r->f_hills != RLK_WG_NONE) {
        const float hills = rlk_wg_sample_field(wg, r->f_hills, qx, qy);
        elev += r->hills_amp * (hills - 0.5f);
    }
    if (r->f_ridge != RLK_WG_NONE) {
        const float mt = smoothstep_between(r->mountain_lo, r->mountain_hi, cont);
        const float ridge = rlk_wg_sample_field(wg, r->f_ridge, qx, qy);
        elev += r->mountain_amp * mt * ridge;
    }

    /* River carve from a derived field (e.g. wg_rivers), on land only. */
    const float h0 = elev - r->sea;
    float river_carve = 0.0f;
    if (h0 > 0.0f && r->river_carve_field != RLK_WG_NONE) {
        const int cidx = wg->nsampled + r->river_carve_field;
        assert(cidx < wg->nsampled + wg->nderived);
        river_carve = ctx->field[cidx];
    }

    /* Fjord carve */
    const float temp = r->f_temp != RLK_WG_NONE ? ctx->field[r->f_temp] : 0.5f;
    const float fjord = fjord_carve(wg, r, wxf, wyf, cont, temp);

    /* Subtract carves */
    elev -= river_carve + fjord;

    /* Altitudinal cooling: temperature drops with height above sea */
    if (r->f_temp != RLK_WG_NONE) {
        const float h = elev - r->sea;
        if (h > 0.0f)
            ctx->field[r->f_temp] -= r->lapse * h;
    }

    /* Write output */
    const int eidx = wg->nsampled + r->slot_elev;
    assert(eidx < wg->nsampled + wg->nderived);
    ctx->field[eidx] = elev;
}

rlk_wg_stage_t
rlk_wg_relief(rlk_wg_relief_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->f_cont != RLK_WG_NONE);
    assert(cfg->slot_elev != RLK_WG_NONE);
    /* Warp fields must be paired */
    assert((cfg->f_warp_x == RLK_WG_NONE) == (cfg->f_warp_y == RLK_WG_NONE));
    /* Resolve zero defaults in place (warp 0->30, hills_amp 0->0.30,
       mountain_lo/hi 0.45/0.65, mountain_amp 0->0.45, lapse 0->0.35,
       fjord_stretch 0->0.25, fjord_width 0->0.05, fjord_depth 0->0.45,
       fjord_steep 0->3.0, fjord_reach 0->0.12, fjord_cold0/1 0.35/0.55). */
    if (cfg->warp == 0.0f)
        cfg->warp = 30.0f;
    if (cfg->hills_amp == 0.0f)
        cfg->hills_amp = 0.30f;
    if (cfg->mountain_hi <= cfg->mountain_lo) {
        cfg->mountain_lo = 0.45f;
        cfg->mountain_hi = 0.65f;
    }
    if (cfg->mountain_amp == 0.0f)
        cfg->mountain_amp = 0.45f;
    if (cfg->lapse == 0.0f)
        cfg->lapse = 0.35f;
    if (cfg->fjord_stretch == 0.0f)
        cfg->fjord_stretch = 0.25f;
    if (cfg->fjord_width <= 0.0f)
        cfg->fjord_width = 0.05f;
    if (cfg->fjord_depth == 0.0f)
        cfg->fjord_depth = 0.45f;
    if (cfg->fjord_steep == 0.0f)
        cfg->fjord_steep = 3.0f;
    if (cfg->fjord_reach <= 0.0f)
        cfg->fjord_reach = 0.12f;
    if (cfg->fjord_cold1 <= cfg->fjord_cold0) {
        cfg->fjord_cold0 = 0.35f;
        cfg->fjord_cold1 = 0.55f;
    }
    return (rlk_wg_stage_t){ .fn = relief_stage, .data = cfg };
}
