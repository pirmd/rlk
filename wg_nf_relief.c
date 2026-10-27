#include "wg_nf_relief.h"

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

void
rlk_wg_nf_relief_init(rlk_wg_nf_relief_t *r) {
    assert(r);
    assert(r->f_cont != RLK_WG_NF_RELIEF_NONE);
    assert(r->slot_elev != RLK_WG_NF_RELIEF_NONE);
    /* Warp fields must be paired */
    assert((r->f_warp_x == RLK_WG_NF_RELIEF_NONE) == (r->f_warp_y == RLK_WG_NF_RELIEF_NONE));
    if (r->warp == 0.0f)
        r->warp = 30.0f;
    if (r->hills_amp == 0.0f)
        r->hills_amp = 0.30f;
    if (r->mountain_hi <= r->mountain_lo) {
        r->mountain_lo = 0.45f;
        r->mountain_hi = 0.65f;
    }
    if (r->mountain_amp == 0.0f)
        r->mountain_amp = 0.45f;
    if (r->lapse == 0.0f)
        r->lapse = 0.35f;
    if (r->river_width_tiles <= 0.0f)
        r->river_width_tiles = 2.0f;
    if (r->relief_span <= 0.0f)
        r->relief_span = 0.40f;
    /* Fjord defaults */
    if (r->fjord_stretch == 0.0f)
        r->fjord_stretch = 0.25f;
    if (r->fjord_width <= 0.0f)
        r->fjord_width = 0.05f;
    if (r->fjord_depth == 0.0f)
        r->fjord_depth = 0.45f;
    if (r->fjord_steep == 0.0f)
        r->fjord_steep = 3.0f;
    if (r->fjord_reach <= 0.0f)
        r->fjord_reach = 0.12f;
    if (r->fjord_cold1 <= r->fjord_cold0) {
        r->fjord_cold0 = 0.35f;
        r->fjord_cold1 = 0.55f;
    }
}

/* Noise-based river: level line of the river field, distance in tiles. */
static void
river_noise(const rlk_wg_nf_t *wg, const rlk_wg_nf_relief_t *r,
            float qx, float qy, float h0, float hum,
            float *out_mask, float *out_carve) {
    assert(r->f_river != RLK_WG_NF_RELIEF_NONE);

    const float rn = rlk_wg_nf_sample_field(wg, r->f_river, qx, qy);
    const float gx = rlk_wg_nf_sample_field(wg, r->f_river, qx + 1.0f, qy) - rn;
    const float gy = rlk_wg_nf_sample_field(wg, r->f_river, qx, qy + 1.0f) - rn;
    float g = sqrtf(gx * gx + gy * gy);
    if (g < RLK_WG_NF_RELIEF_MIN_GRAD)
        g = RLK_WG_NF_RELIEF_MIN_GRAD;

    /* Distance to the centre line in tiles */
    const float dist = fabsf(rn - 0.5f) / g;
    const float hn = rlk_clampf(h0 / r->relief_span, 0.0f, 1.0f);
    /* Width: wider near coast, thinner in mountains; scales with humidity */
    const float wet = (0.5f + hum) / 1.0f;
    const float half = r->river_width_tiles * wet * (1.0f - 0.6f * hn);
    if (half <= 1e-3f)
        return;

    const float m = rlk_clampf(1.0f - dist / half, 0.0f, 1.0f);
    if (m > *out_mask)
        *out_mask = m;

    const float vall = rlk_clampf(1.0f - dist / (3.0f * half), 0.0f, 1.0f);
    const float cv = r->river_carve * vall * vall;
    if (cv > *out_carve)
        *out_carve = cv;
}

/* Fjord carve at (px, py): straight, deep valley near cold coasts. */
static float
fjord_carve(const rlk_wg_nf_t *wg, const rlk_wg_nf_relief_t *r,
            float px, float py, float cont, float temp) {
    if (r->f_fjord == RLK_WG_NF_RELIEF_NONE)
        return 0.0f;
    if (!(cont > r->sea))
        return 0.0f;

    /* Coast factor: active near the shoreline */
    const float coast = 1.0f - smoothstep_between(0.0f, r->fjord_reach, cont - r->sea);
    if (coast <= 0.0f)
        return 0.0f;

    /* Cold factor: only in cold regions */
    float cold = 1.0f;
    if (r->f_temp != RLK_WG_NF_RELIEF_NONE)
        cold = 1.0f - smoothstep_between(r->fjord_cold0, r->fjord_cold1, temp);
    if (cold <= 0.0f)
        return 0.0f;

    /* Rotate and stretch coordinates onto the fjord axis */
    const float u = px * r->fjord_dir[0] + py * r->fjord_dir[1];
    const float v = -px * r->fjord_dir[1] + py * r->fjord_dir[0];

    const float fn = rlk_wg_nf_sample_field(wg, r->f_fjord, u * r->fjord_stretch, v);
    const float fr = 1.0f - fabsf(2.0f * fn - 1.0f);
    const float vv = rlk_clampf((fr - (1.0f - r->fjord_width)) / r->fjord_width, 0.0f, 1.0f);
    const float steeper = vv * r->fjord_steep;
    const float profile = steeper > 1.0f
        ? 1.0f
        : steeper * steeper * (3.0f - 2.0f * steeper);

    return r->fjord_depth * coast * cold * profile;
}

void
rlk_wg_nf_relief_stage(rlk_wg_nf_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_nf_relief_t *r = (const rlk_wg_nf_relief_t *)data;
    const rlk_wg_nf_t *wg = ctx->wg;

    const float px = (float)ctx->wx;
    const float py = (float)ctx->wy;

    /* Continent value (sampled at original position by the pipeline) */
    const float cont = ctx->field[r->f_cont];

    /* Domain warp: displace position using warp fields */
    float qx = px, qy = py;
    if (r->f_warp_x != RLK_WG_NF_RELIEF_NONE && r->f_warp_y != RLK_WG_NF_RELIEF_NONE) {
        const float vx = ctx->field[r->f_warp_x];
        const float vy = ctx->field[r->f_warp_y];
        qx += (vx - 0.5f) * 2.0f * r->warp;
        qy += (vy - 0.5f) * 2.0f * r->warp;
    }

    /* Base elevation: cont + hills + mountains */
    float elev = cont;
    if (r->f_hills != RLK_WG_NF_RELIEF_NONE) {
        const float hills = rlk_wg_nf_sample_field(wg, r->f_hills, qx, qy);
        elev += r->hills_amp * (hills - 0.5f);
    }
    if (r->f_ridge != RLK_WG_NF_RELIEF_NONE) {
        const float mt = smoothstep_between(r->mountain_lo, r->mountain_hi, cont);
        const float ridge = rlk_wg_nf_sample_field(wg, r->f_ridge, qx, qy);
        elev += r->mountain_amp * mt * ridge;
    }

    const float h0 = elev - r->sea;
    float river_mask = 0.0f;
    float river_carve = 0.0f;

    /* Rivers: from a derived field (e.g. wg_nf_rivers) or noise */
    if (h0 > 0.0f) {
        if (r->river_carve_field != RLK_WG_NF_RELIEF_NONE) {
            /* Read carve from a derived field produced by another stage.
               The river mask is NOT recomputed here: it lives in its own
               derived slot (written by wg_nf_rivers). Only the carve is
               subtracted from the elevation below. */
            const int cidx = wg->nsampled + r->river_carve_field;
            assert(cidx >= 0 && cidx < RLK_WG_NF_MAX_FIELDS);
            river_carve = ctx->field[cidx];
        } else if (r->f_river != RLK_WG_NF_RELIEF_NONE) {
            const float hum = 0.5f; /* no humidity field for now */
            river_noise(wg, r, qx, qy, h0, hum, &river_mask, &river_carve);
        }
    }

    /* Fjord carve */
    const float temp = r->f_temp != RLK_WG_NF_RELIEF_NONE ? ctx->field[r->f_temp] : 0.5f;
    const float fjord = fjord_carve(wg, r, px, py, cont, temp);

    /* Subtract carves */
    elev -= river_carve + fjord;

    /* Altitudinal cooling: temperature drops with height above sea */
    if (r->f_temp != RLK_WG_NF_RELIEF_NONE) {
        const float h = elev - r->sea;
        if (h > 0.0f)
            ctx->field[r->f_temp] -= r->lapse * h;
    }

    /* Write outputs */
    const int eidx = wg->nsampled + r->slot_elev;
    assert(eidx >= 0 && eidx < RLK_WG_NF_MAX_FIELDS);
    ctx->field[eidx] = elev;

    if (r->slot_river != RLK_WG_NF_RELIEF_NONE) {
        const int ridx = wg->nsampled + r->slot_river;
        assert(ridx >= 0 && ridx < RLK_WG_NF_MAX_FIELDS);
        ctx->field[ridx] = river_mask;
    }
}

rlk_err_t
rlk_wg_nf_relief_add(rlk_wg_nf_t *wg, rlk_wg_nf_relief_t *r) {
    assert(wg && r);
    rlk_wg_nf_relief_init(r);
    return rlk_wg_nf_add_stage(wg, rlk_wg_nf_relief_stage, r);
}
