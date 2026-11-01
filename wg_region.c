#include "wg_region.h"

#include <assert.h>
#include "hash.h"
#include "fmath.h"
#include "noise.h"

/* Region type of a Voronoi cell: decorrelated hash of the cell id, in [0, ntypes). */
static uint8_t
cell_type(uint64_t cell_id, int ntypes) {
    return (uint8_t)((rlk_hash_sub(cell_id, 3) >> 33) % (uint64_t)ntypes);
}

static void
region_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_region_cfg_t *cfg = (const rlk_wg_region_cfg_t *)data;
    const rlk_wg_t *wg = ctx->wg;

    const float cell_size = (float)cfg->cell;
    const float blend = (float)cfg->blend;
    const float wxf = (float)ctx->wx + 0.5f, wyf = (float)ctx->wy + 0.5f;
    const uint64_t seed = rlk_hash_sub(rlk_hash_sub(wg->seed, 0x9E0F5C3D11ULL), cfg->salt);

    /* Voronoi partition at the tile centre. */
    rlk_noise_t noise = { .seed = seed };
    rlk_noise_cellular_t near;  /* F1/F2 + id of the closest feature point */
    rlk_noise_cellular(&noise, wxf, wyf, cell_size, &near);

    /* Region types: of the closest cell, and of the cell containing the
       point (its id is the lattice hash of its integer coords). */
    const uint8_t type_near = cell_type(near.id, cfg->ntypes);
    const int bx = (int)floorf(wxf / cell_size);
    const int by = (int)floorf(wyf / cell_size);
    const uint8_t type_here = cell_type(rlk_hash_combine2(seed, bx, by), cfg->ntypes);

    /* Blend: full weight on the closest cell deep inside it, mixed with the
       containing cell within `blend` of a border (d2 - d1 ~ 0 on a border,
       grows towards the cell centre). blend < 0 = hard borders: the
       nearest cell always wins. */
    const float mix = blend > 0.0f
        ? rlk_clampf((near.d2 - near.d1) / blend, 0.0f, 1.0f)
        : 1.0f;
    const rlk_wg_region_type_t *tnear = &cfg->types[type_near];
    const rlk_wg_region_type_t *there = &cfg->types[type_here];
    for (int f = 0; f < wg->nsampled; f++) {
        const float bias = mix * tnear->bias[f] + (1.0f - mix) * there->bias[f];
        const float gain = mix * tnear->gain[f] + (1.0f - mix) * there->gain[f];
        ctx->field[f] = (ctx->field[f] + bias) * (1.0f + gain);
    }
}

rlk_wg_stage_t
rlk_wg_region(rlk_wg_region_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->ntypes >= 1 && cfg->ntypes <= RLK_WG_REGION_MAX_TYPES);
    /* Resolve zero defaults in place (cell 0->128, blend 0->cell/4). */
    if (cfg->cell == 0)
        cfg->cell = 128;
    if (cfg->blend == 0)
        cfg->blend = cfg->cell / 4;
    assert(cfg->cell > 0);
    return (rlk_wg_stage_t){ .fn = region_stage, .data = cfg };
}
