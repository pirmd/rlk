#include "wg_drain.h"

#include <assert.h>
#include <math.h>

static void
drain_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    rlk_wg_drain_cfg_t fc = *(const rlk_wg_drain_cfg_t *)data;
    if (fc.acc_scale == 0.0f)
        fc.acc_scale = 1.0f;
    const rlk_wg_drain_cfg_t *f = &fc;
    const rlk_wg_t *wg = ctx->wg;

    int i, j;
    int inside = rlk_grid_cell_of(f->elev, ctx->wx, ctx->wy, &i, &j);

    float acc_val = 0.0f;
    float acc_log = 0.0f;
    float dir_val = (float)RLK_DRAIN_OUTLET;
    float lake_val = 0.0f;

    if (inside) {
        const size_t c = (size_t)j * (size_t)f->drain->lattice.w + (size_t)i;
        acc_val = (float)f->drain->acc[c] / f->acc_scale;
        /* Normalized log10(acc) in [0,1]: acc <= w*h by construction, so the
           lattice size is the map-wide maximum — no runtime pass needed
           (stages are pure per tile). Hack's law input for wg_rivers. */
        const float norm = log10f((float)(f->drain->lattice.w * f->drain->lattice.h) + 1.0f);
        acc_log = log10f((float)f->drain->acc[c] + 1.0f) / norm;
        dir_val = (float)f->drain->dir[c];
        lake_val = rlk_drain_lake_depth(f->drain, f->elev, f->sea, i, j);
    }

    if (f->slot_acc != RLK_WG_DRAIN_NONE) {
        const int idx = wg->nsampled + f->slot_acc;
        assert(idx >= 0 && idx < RLK_WG_MAX_FIELDS);
        ctx->field[idx] = acc_val;
    }
    if (f->slot_dir != RLK_WG_DRAIN_NONE) {
        const int idx = wg->nsampled + f->slot_dir;
        assert(idx >= 0 && idx < RLK_WG_MAX_FIELDS);
        ctx->field[idx] = dir_val;
    }
    if (f->slot_lake != RLK_WG_DRAIN_NONE) {
        const int idx = wg->nsampled + f->slot_lake;
        assert(idx >= 0 && idx < RLK_WG_MAX_FIELDS);
        ctx->field[idx] = lake_val;
    }
    if (f->slot_acc_log != RLK_WG_DRAIN_NONE) {
        const int idx = wg->nsampled + f->slot_acc_log;
        assert(idx >= 0 && idx < RLK_WG_MAX_FIELDS);
        ctx->field[idx] = acc_log;
    }
}

rlk_wg_stage_t
rlk_wg_drain(const rlk_wg_drain_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->drain && cfg->elev);
    assert(rlk_lattice_same(&cfg->drain->lattice, &cfg->elev->lattice));
    rlk_wg_stage_t st = { .fn = drain_stage, .data = cfg };
    if (cfg->slot_acc != RLK_WG_DRAIN_NONE)
        st.writes.derived |= RLK_WG_SLOT(cfg->slot_acc);
    if (cfg->slot_dir != RLK_WG_DRAIN_NONE)
        st.writes.derived |= RLK_WG_SLOT(cfg->slot_dir);
    if (cfg->slot_lake != RLK_WG_DRAIN_NONE)
        st.writes.derived |= RLK_WG_SLOT(cfg->slot_lake);
    if (cfg->slot_acc_log != RLK_WG_DRAIN_NONE)
        st.writes.derived |= RLK_WG_SLOT(cfg->slot_acc_log);
    return st;
}
