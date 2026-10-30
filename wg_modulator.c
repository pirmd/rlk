#include "wg_modulator.h"

#include <assert.h>
#include "hash.h"
#include "fmath.h"
#include "noise.h"

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

    rlk_noise_t cn;
    rlk_noise_seed(&cn, pseed);
    rlk_noise_cell_t c;
    rlk_noise_cell(&cn, wxf, wyf, cell, &c);

    /* Type of the closest cell, and of the cell containing the point. */
    const uint8_t t1 = (uint8_t)((rlk_hash_sub(c.id, 3) >> 33) % (uint64_t)m->ntypes);
    const int bx = (int)floorf(wxf / cell);
    const int by = (int)floorf(wyf / cell);
    const uint64_t h0 = rlk_hash_combine2(pseed, bx, by);
    const uint8_t t0 = (uint8_t)((rlk_hash_sub(h0, 3) >> 33) % (uint64_t)m->ntypes);

    /* Smooth blend: full weight on the closest cell deep inside it, mixed
       with the containing cell within `blend` of a border (d2 - d1 ~ 0 on
       a border, grows towards the cell center). */
    const float w = rlk_clampf((c.d2 - c.d1) / blend, 0.0f, 1.0f);
    const rlk_wg_modulator_type_t *ta = &m->types[t1];
    const rlk_wg_modulator_type_t *tb = &m->types[t0];
    const int nf = wg->nsampled;
    for (int f = 0; f < nf; f++) {
        const float bias = w * ta->bias[f] + (1.0f - w) * tb->bias[f];
        const float gain = w * ta->gain[f] + (1.0f - w) * tb->gain[f];
        ctx->field[f] = (ctx->field[f] + bias) * (1.0f + gain);
    }
}

rlk_wg_stage_t
rlk_wg_modulator(const rlk_wg_modulator_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->ntypes >= 1 && cfg->ntypes <= RLK_WG_MODULATOR_MAX_TYPES);
    rlk_wg_modulator_cfg_t m = *cfg;
    modulator_resolve(&m);
    assert(m.cell >= 16.0f);
    assert(m.blend <= 0.5f * m.cell);
    /* The modulator reads and writes every sampled field in place. */
    return (rlk_wg_stage_t){ .fn = modulator_stage, .data = cfg,
                             .reads = { .sampled = RLK_WG_SLOTS_ALL },
                             .writes = { .sampled = RLK_WG_SLOTS_ALL } };
}
