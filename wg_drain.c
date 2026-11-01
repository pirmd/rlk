#include "wg_drain.h"

#include <assert.h>

static void
drain_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_drain_cfg_t *cfg = (const rlk_wg_drain_cfg_t *)data;
    const rlk_wg_t *wg = ctx->wg;

    /* Slot bounds are checked here (the constructor does not know the wg). */
    assert(cfg->slot_acc  == RLK_WG_NONE || wg->nsampled + (int)cfg->slot_acc  < RLK_WG_MAX_FIELDS);
    assert(cfg->slot_dir  == RLK_WG_NONE || wg->nsampled + (int)cfg->slot_dir  < RLK_WG_MAX_FIELDS);
    assert(cfg->slot_fill == RLK_WG_NONE || wg->nsampled + (int)cfg->slot_fill < RLK_WG_MAX_FIELDS);

    const int c = rlk_lattice_index(&cfg->elev->lattice, ctx->wx, ctx->wy);

    float acc_val = 0.0f;
    float dir_val = (float)RLK_DRAIN_OUTLET;
    float fill_val = 0.0f;

    if (c >= 0) {
        const rlk_drain_t *d = cfg->drain;
        const size_t k = (size_t)c;
        const int w = d->lattice.w;
        acc_val = (float)d->acc[k] / ((float)d->lattice.w * (float)d->lattice.h);
        dir_val = (float)d->dir[k];
        fill_val = rlk_drain_fill_depth(d, cfg->elev, c % w, c / w);
    }

    if (cfg->slot_acc != RLK_WG_NONE)
        ctx->field[wg->nsampled + cfg->slot_acc] = acc_val;
    if (cfg->slot_dir != RLK_WG_NONE)
        ctx->field[wg->nsampled + cfg->slot_dir] = dir_val;
    if (cfg->slot_fill != RLK_WG_NONE)
        ctx->field[wg->nsampled + cfg->slot_fill] = fill_val;
}

rlk_wg_stage_t
rlk_wg_drain(rlk_wg_drain_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->drain && cfg->elev);
    assert(rlk_lattice_same(&cfg->drain->lattice, &cfg->elev->lattice));
    return (rlk_wg_stage_t){ .fn = drain_stage, .data = cfg };
}
