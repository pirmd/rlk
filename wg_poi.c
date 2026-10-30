#include "wg_poi.h"

#include <assert.h>
#include <stddef.h>

/* Resolve zero defaults: density 0->0.001, size 0->32, min_separation 0->size. */
static void
poi_resolve(rlk_wg_poi_cfg_t *cfg) {
    if (cfg->density == 0.0f)
        cfg->density = 0.001f;
    if (cfg->size == 0)
        cfg->size = 32;
    if (cfg->min_separation == 0)
        cfg->min_separation = (int)cfg->size;
}

static void
poi_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    rlk_wg_poi_cfg_t rc = *(const rlk_wg_poi_cfg_t *)data;
    poi_resolve(&rc);
    const rlk_wg_poi_cfg_t *cfg = &rc;
    const rlk_wg_t *wg = ctx->wg;

    /* Field threshold. */
    if (cfg->field >= (uint8_t)(wg->nsampled + wg->nderived))
        return;
    if (ctx->field[cfg->field] <= cfg->threshold)
        return;

    /* Density: deterministic per-tile random. */
    float r = rlk_wg_rand(wg, ctx->wx, ctx->wy, cfg->salt);
    if (r >= cfg->density)
        return;

    /* Drain constraint: near river. */
    if (cfg->drain) {
        const int idx = rlk_lattice_index(&cfg->drain->lattice, ctx->wx, ctx->wy);
        if (idx < 0)
            return;
        if ((float)cfg->drain->acc[(size_t)idx] < cfg->min_acc)
            return;
    }

    /* Grid constraint: elevation/other range. */
    if (cfg->grid) {
        const float wxf = (float)ctx->wx + 0.5f;
        const float wyf = (float)ctx->wy + 0.5f;
        float v = rlk_grid_sample(cfg->grid, wxf, wyf);
        if (v < cfg->grid_min || v > cfg->grid_max)
            return;
    }

    /* Deduplication: no other POI of same type within min_separation. */
    if (cfg->min_separation > 0) {
        const rlk_poi_t *near = rlk_poi_find(cfg->list,
                                             ctx->wx, ctx->wy,
                                             cfg->min_separation,
                                             cfg->type);
        if (near)
            return;
    }

    /* Create POI: anchor at the tile, square footprint of `size` tiles. */
    const int hs = (int)cfg->size / 2;
    rlk_poi_t poi = {
        .x = ctx->wx,
        .y = ctx->wy,
        .bbox = { ctx->wx - hs, ctx->wy - hs, (int)cfg->size, (int)cfg->size },
        .type = cfg->type,
        .seed = rlk_poi_seed(wg->seed, ctx->wx, ctx->wy, cfg->type, cfg->salt),
        .gen = 0,
        .level = 0,
    };
    (void)rlk_poi_push(cfg->list, &poi);
}

rlk_wg_stage_t
rlk_wg_poi(const rlk_wg_poi_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->list);
    return (rlk_wg_stage_t){ .fn = poi_stage, .data = cfg };
}
