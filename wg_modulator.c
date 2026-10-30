#include "wg_modulator.h"

#include <assert.h>
#include <float.h>
#include "hash.h"
#include "fmath.h"

#define NEIGHBORHOOD 25  /* 5x5 cells */

/* Resolve zero defaults (cell 0->128, blend 0->cell/4). */
static void
modulator_resolve(rlk_wg_modulator_cfg_t *m) {
    if (m->cell == 0.0f)
        m->cell = 128.0f;
    if (m->blend == 0.0f)
        m->blend = 0.25f * m->cell;
}

static void
modulator_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    rlk_wg_modulator_cfg_t mc = *(const rlk_wg_modulator_cfg_t *)data;
    modulator_resolve(&mc);
    const rlk_wg_modulator_cfg_t *m = &mc;
    const rlk_wg_t *wg = ctx->wg;

    const float cell = m->cell;
    const float blend = m->blend;
    const float wxf = (float)ctx->wx + 0.5f, wyf = (float)ctx->wy + 0.5f;
    const uint64_t pseed = rlk_hash_sub(rlk_hash_sub(wg->seed, 0x9E0F5C3D11ULL), m->salt);

    float d[NEIGHBORHOOD];
    uint8_t t[NEIGHBORHOOD];
    int n = 0;
    float dmin = FLT_MAX;

    for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
            int cx = (int)floorf(wxf / cell) + i;
            int cy = (int)floorf(wyf / cell) + j;
            uint64_t h = rlk_hash_combine2(pseed, cx, cy);
            float fx = 0.15f + 0.70f * (float)(rlk_hash_sub(h, 1) >> 40) * (1.0f / 16777216.0f);
            float fy = 0.15f + 0.70f * (float)(rlk_hash_sub(h, 2) >> 40) * (1.0f / 16777216.0f);
            float qx = ((float)cx + fx) * cell - wxf;
            float qy = ((float)cy + fy) * cell - wyf;
            d[n] = rlk_dist_euclideanf(0.0f, 0.0f, qx, qy);
            t[n] = (uint8_t)((rlk_hash_sub(h, 3) >> 33) % (uint64_t)m->ntypes);
            if (d[n] < dmin) dmin = d[n];
            n++;
        }
    }

    /* Weighted average of bias and gain. */
    float bias[RLK_WG_MAX_FIELDS] = {0};
    float gain[RLK_WG_MAX_FIELDS] = {0};
    float sum = 0.0f;
    const int nf = wg->nsampled;

    for (int k = 0; k < n; k++) {
        float w = rlk_clampf(1.0f - (d[k] - dmin) / blend, 0.0f, 1.0f);
        if (w <= 0.0f)
            continue;
        sum += w;
        const rlk_wg_modulator_type_t *ty = &m->types[t[k]];
        for (int f = 0; f < nf; f++) {
            bias[f] += w * ty->bias[f];
            gain[f] += w * ty->gain[f];
        }
    }

    const float inv = sum > 0.0f ? 1.0f / sum : 1.0f;
    for (int f = 0; f < nf; f++)
        ctx->field[f] = (ctx->field[f] + bias[f] * inv) * (1.0f + gain[f] * inv);
}

rlk_wg_stage_t
rlk_wg_modulator(const rlk_wg_modulator_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->ntypes >= 1 && cfg->ntypes <= RLK_WG_MODULATOR_MAX_TYPES);
    rlk_wg_modulator_cfg_t m = *cfg;
    modulator_resolve(&m);
    assert(m.cell >= 16.0f);
    assert(m.blend <= 0.5f * m.cell);
    return (rlk_wg_stage_t){ .fn = modulator_stage, .data = cfg };
}
