#include "wg_nf_grid.h"

#include <assert.h>

void
rlk_wg_nf_grid_init(rlk_wg_nf_grid_t *g) {
    assert(g);
    assert(g->grid);
    if (g->scale == 0.0f)
        g->scale = 1.0f;
}

void
rlk_wg_nf_grid_stage(rlk_wg_nf_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_nf_grid_t *g = (const rlk_wg_nf_grid_t *)data;
    const rlk_wg_nf_t *wg = ctx->wg;

    const int idx = wg->nsampled + g->slot;
    assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);

    const float wxf = (float)ctx->wx + 0.5f;
    const float wyf = (float)ctx->wy + 0.5f;
    const float v = rlk_grid_sample(g->grid, wxf, wyf);
    ctx->field[idx] = v * g->scale + g->offset;
}

rlk_err_t
rlk_wg_nf_grid_add(rlk_wg_nf_t *wg, rlk_wg_nf_grid_t *g) {
    assert(wg && g);
    rlk_wg_nf_grid_init(g);
    return rlk_wg_nf_add_stage(wg, rlk_wg_nf_grid_stage, g);
}
