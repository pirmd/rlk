#include "wg_relief.h"

#include <assert.h>
#include <math.h>

#include "fmath.h"

/* Smoothstep between lo and hi: t mapped from [lo,hi] to [0,1], clamped at
   both ends. Requires hi > lo. */
static inline float
smoothstep_between(float lo, float hi, float t) {
    assert(hi > lo);
    return rlk_smoothstep(rlk_clampf((t - lo) / (hi - lo), 0.0f, 1.0f));
}

/* Fjord carve at (wxf, wyf): straight, deep valley near cold coasts. */
static float
fjord_carve(const rlk_wg_t *wg, const rlk_wg_relief_cfg_t *cfg,
            float wxf, float wyf, float cont, float temp) {
    if (cfg->f_fjord == RLK_WG_NONE)
        return 0.0f;
    if (!(cont > cfg->sea))
        return 0.0f;

    /* Coast factor: active near the shoreline */
    const float coast = 1.0f - smoothstep_between(0.0f, cfg->fjord_reach, cont - cfg->sea);
    if (coast <= 0.0f)
        return 0.0f;

    /* Cold factor: only in cold regions */
    float cold = 1.0f;
    if (cfg->f_temp != RLK_WG_NONE)
        cold = 1.0f - smoothstep_between(cfg->fjord_cold0, cfg->fjord_cold1, temp);
    if (cold <= 0.0f)
        return 0.0f;

    /* Rotate and stretch onto the fjord axis: `along` runs with fjord_dir,
       `across` is the offset from the fjord centreline. */
    const float along = wxf * cfg->fjord_dir[0] + wyf * cfg->fjord_dir[1];
    const float across = -wxf * cfg->fjord_dir[1] + wyf * cfg->fjord_dir[0];

    const float noise = rlk_wg_sample_field(wg, cfg->f_fjord, along * cfg->fjord_stretch, across);
    const float ridge = 1.0f - fabsf(2.0f * noise - 1.0f);  /* 1 on the centreline */
    const float t = rlk_clampf((ridge - (1.0f - cfg->fjord_width)) / cfg->fjord_width, 0.0f, 1.0f);
    const float profile = smoothstep_between(0.0f, 1.0f, t * cfg->fjord_steep);

    return cfg->fjord_depth * coast * cold * profile;
}

static void
relief_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_relief_cfg_t *cfg = (const rlk_wg_relief_cfg_t *)data;
    const rlk_wg_t *wg = ctx->wg;

    /* Sampled field indices must be in [0, nsampled) — checked here (the
       constructor does not know the wg). */
    const uint8_t f_in[] = { cfg->f_cont, cfg->f_hills, cfg->f_ridge,
                             cfg->f_warp_x, cfg->f_warp_y, cfg->f_fjord, cfg->f_temp };
    for (int i = 0; i < (int)(sizeof f_in / sizeof f_in[0]); i++)
        assert(f_in[i] == RLK_WG_NONE || (int)f_in[i] < wg->nsampled);

    const float wxf = (float)ctx->wx + 0.5f;   /* tile centre */
    const float wyf = (float)ctx->wy + 0.5f;

    /* Continent value (sampled at the original position by the field pass) */
    const float cont = ctx->field[cfg->f_cont];

    /* Domain warp: displace the sampling position using the warp fields */
    float px = wxf, py = wyf;
    if (cfg->f_warp_x != RLK_WG_NONE && cfg->f_warp_y != RLK_WG_NONE) {
        const float vx = ctx->field[cfg->f_warp_x];
        const float vy = ctx->field[cfg->f_warp_y];
        px += (vx - 0.5f) * 2.0f * cfg->warp;
        py += (vy - 0.5f) * 2.0f * cfg->warp;
    }

    /* Base elevation: cont + hills + mountains */
    float elev = cont;
    if (cfg->f_hills != RLK_WG_NONE) {
        const float hills = rlk_wg_sample_field(wg, cfg->f_hills, px, py);
        elev += cfg->hills_amp * (hills - 0.5f);
    }
    if (cfg->f_ridge != RLK_WG_NONE) {
        const float mtn = smoothstep_between(cfg->mountain_lo, cfg->mountain_hi, cont);
        const float ridge = rlk_wg_sample_field(wg, cfg->f_ridge, px, py);
        elev += cfg->mountain_amp * mtn * ridge;
    }

    /* River carve from a derived field (e.g. wg_rivers), on land only. */
    float river_carve = 0.0f;
    if (elev > cfg->sea && cfg->river_carve_field != RLK_WG_NONE) {
        const int carve_idx = wg->nsampled + cfg->river_carve_field;
        assert(carve_idx < RLK_WG_MAX_FIELDS);
        river_carve = ctx->field[carve_idx];
        /* Taper the carve near the coast: a valley must reach the sea, not
           dig below it (a sub-sea carve reads as a lake). */
        const float room = elev - cfg->sea;
        if (river_carve > room)
            river_carve = room;
    }

    /* Fjord carve */
    const float temp = cfg->f_temp != RLK_WG_NONE ? ctx->field[cfg->f_temp] : 0.5f;
    const float fjord = fjord_carve(wg, cfg, wxf, wyf, cont, temp);

    /* Subtract carves */
    elev -= river_carve + fjord;

    /* Altitudinal cooling: temperature drops with height above sea */
    if (cfg->f_temp != RLK_WG_NONE) {
        const float height = elev - cfg->sea;
        if (height > 0.0f)
            ctx->field[cfg->f_temp] -= cfg->lapse * height;
    }

    /* Write output */
    const int elev_idx = wg->nsampled + cfg->slot_elev;
    assert(elev_idx < RLK_WG_MAX_FIELDS);
    ctx->field[elev_idx] = elev;
}

rlk_wg_stage_t
rlk_wg_relief(rlk_wg_relief_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->f_cont != RLK_WG_NONE);
    assert(cfg->slot_elev != RLK_WG_NONE);
    /* Warp fields must be paired */
    assert((cfg->f_warp_x == RLK_WG_NONE) == (cfg->f_warp_y == RLK_WG_NONE));
    /* A zero fjord_dir with fjords enabled would carve a constant slab */
    if (cfg->f_fjord != RLK_WG_NONE)
        assert(cfg->fjord_dir[0] != 0.0f || cfg->fjord_dir[1] != 0.0f);
    /* Resolve zero defaults in place; the default of each parameter is
       documented on the cfg field in the header. */
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
