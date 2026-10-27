#include "wg_nf_poi.h"

#include <assert.h>
#include <stddef.h>

#include "hash.h"

void
rlk_wg_nf_poi_init(rlk_wg_nf_poi_t *cfg) {
    assert(cfg);
    assert(cfg->list);
    if (cfg->density == 0.0f)
        cfg->density = 0.001f;
    if (cfg->size == 0)
        cfg->size = 32;
    if (cfg->min_separation == 0)
        cfg->min_separation = (int)cfg->size;
}

void
rlk_wg_nf_poi_stage(rlk_wg_nf_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_nf_poi_t *cfg = (const rlk_wg_nf_poi_t *)data;
    const rlk_wg_nf_t *wg = ctx->wg;

    /* Field threshold. */
    if (cfg->field >= (uint8_t)(wg->nsampled + wg->nderived))
        return;
    if (ctx->field[cfg->field] <= cfg->threshold)
        return;

    /* Density: deterministic per-tile random. */
    float r = rlk_wg_nf_rand(wg, ctx->wx, ctx->wy, cfg->salt);
    if (r >= cfg->density)
        return;

    /* Flow constraint: near river. */
    uint8_t flags = 0;
    if (cfg->flow) {
        assert(cfg->flow_grid);
        int i, j;
        if (!rlk_grid_cell_of(cfg->flow_grid, ctx->wx, ctx->wy, &i, &j))
            return;
        const rlk_wg_flow_t *f = cfg->flow;
        const size_t c = (size_t)j * (size_t)f->w + (size_t)i;
        if ((float)f->acc[c] < cfg->min_acc)
            return;
        flags |= RLK_WG_POI_NEAR_RIVER;
    }

    /* Grid constraint: elevation/other range. */
    if (cfg->grid) {
        const float wxf = (float)ctx->wx + 0.5f;
        const float wyf = (float)ctx->wy + 0.5f;
        float v = rlk_grid_sample(cfg->grid, wxf, wyf);
        if (v < cfg->grid_min || v > cfg->grid_max)
            return;
        /* Heuristic: if grid range is above 0.5, mark as ON_HILL. */
        if (cfg->grid_min > 0.5f)
            flags |= RLK_WG_POI_ON_HILL;
    }

    /* Deduplication: no other POI of same kind within min_separation. */
    if (cfg->min_separation > 0) {
        const rlk_wg_poi_t *near = rlk_wg_poi_find(cfg->list,
                                                   ctx->wx, ctx->wy,
                                                   cfg->min_separation,
                                                   cfg->kind);
        if (near)
            return;
    }

    /* Create POI. */
    rlk_wg_poi_t poi = {
        .wx = ctx->wx,
        .wy = ctx->wy,
        .kind = cfg->kind,
        .seed = rlk_wg_poi_seed(wg->seed, ctx->wx, ctx->wy, cfg->kind, cfg->salt),
        .size = cfg->size,
        .flags = flags,
    };
    (void)rlk_wg_poi_push(cfg->list, &poi);
}

rlk_err_t
rlk_wg_nf_poi_add(rlk_wg_nf_t *wg, rlk_wg_nf_poi_t *cfg) {
    assert(wg && cfg);
    rlk_wg_nf_poi_init(cfg);
    return rlk_wg_nf_add_stage(wg, rlk_wg_nf_poi_stage, cfg);
}
