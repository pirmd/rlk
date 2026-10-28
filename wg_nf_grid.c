#include "wg_nf_grid.h"

#include <assert.h>

static void
grid_stage(rlk_wg_nf_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_nf_grid_cfg_t *g = (const rlk_wg_nf_grid_cfg_t *)data;
    const rlk_wg_nf_t *wg = ctx->wg;

    const int idx = wg->nsampled + g->slot;
    assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);

    const float scale = g->scale == 0.0f ? 1.0f : g->scale;
    const float wxf = (float)ctx->wx + 0.5f;
    const float wyf = (float)ctx->wy + 0.5f;
    const float v = rlk_grid_sample(g->grid, wxf, wyf);
    ctx->field[idx] = v * scale + g->offset;
}

rlk_wg_nf_stage_t
rlk_wg_nf_grid(const rlk_wg_nf_grid_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->grid);
    return (rlk_wg_nf_stage_t){ .fn = grid_stage, .data = cfg };
}
