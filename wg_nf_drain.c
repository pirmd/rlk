#include "wg_nf_drain.h"

#include <assert.h>

void
rlk_wg_nf_drain_init(rlk_wg_nf_drain_t *f) {
    assert(f);
    assert(f->drain && f->elev);
    assert(rlk_lattice_same(&f->drain->lattice, &f->elev->lattice));
    if (f->acc_scale == 0.0f)
        f->acc_scale = 1.0f;
}

void
rlk_wg_nf_drain_stage(rlk_wg_nf_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_nf_drain_t *f = (const rlk_wg_nf_drain_t *)data;
    const rlk_wg_nf_t *wg = ctx->wg;

    int i, j;
    int inside = rlk_grid_cell_of(f->elev, ctx->wx, ctx->wy, &i, &j);

    float acc_val = 0.0f;
    float dir_val = (float)RLK_DRAIN_OUTLET;
    float lake_val = 0.0f;

    if (inside) {
        const size_t c = (size_t)j * (size_t)f->drain->lattice.w + (size_t)i;
        acc_val = (float)f->drain->acc[c] / f->acc_scale;
        dir_val = (float)f->drain->dir[c];
        lake_val = rlk_drain_lake_depth(f->drain, f->elev, f->sea, i, j);
    }

    if (f->slot_acc != RLK_WG_NF_DRAIN_NONE) {
        const int idx = wg->nsampled + f->slot_acc;
        assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);
        ctx->field[idx] = acc_val;
    }
    if (f->slot_dir != RLK_WG_NF_DRAIN_NONE) {
        const int idx = wg->nsampled + f->slot_dir;
        assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);
        ctx->field[idx] = dir_val;
    }
    if (f->slot_lake != RLK_WG_NF_DRAIN_NONE) {
        const int idx = wg->nsampled + f->slot_lake;
        assert(idx >= 0 && idx < RLK_WG_NF_MAX_FIELDS);
        ctx->field[idx] = lake_val;
    }
}

rlk_err_t
rlk_wg_nf_drain_add(rlk_wg_nf_t *wg, rlk_wg_nf_drain_t *f) {
    assert(wg && f);
    rlk_wg_nf_drain_init(f);
    return rlk_wg_nf_add_stage(wg, rlk_wg_nf_drain_stage, f);
}
