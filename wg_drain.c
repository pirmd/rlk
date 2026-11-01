#include "wg_drain.h"

#include <assert.h>

/* Write v into derived slot `slot` (RLK_WG_NONE = skip). */
static void
set_derived(rlk_wg_ctx_t *ctx, uint8_t slot, float v) {
    if (slot == RLK_WG_NONE)
        return;
    const int idx = ctx->wg->nsampled + slot;
    assert(idx < RLK_WG_MAX_FIELDS);
    ctx->field[idx] = v;
}

static void
drain_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_drain_cfg_t *f = (const rlk_wg_drain_cfg_t *)data;

    const int c = rlk_lattice_index(&f->elev->lattice, ctx->wx, ctx->wy);

    float acc_val = 0.0f;
    float dir_val = (float)RLK_DRAIN_OUTLET;
    float fill_val = 0.0f;

    if (c >= 0) {
        const rlk_drain_t *d = f->drain;
        const size_t k = (size_t)c;
        const int w = d->lattice.w;
        acc_val = (float)d->acc[k] / f->acc_scale;
        dir_val = (float)d->dir[k];
        fill_val = rlk_drain_fill_depth(d, f->elev, c % w, c / w);
    }

    set_derived(ctx, f->slot_acc, acc_val);
    set_derived(ctx, f->slot_dir, dir_val);
    set_derived(ctx, f->slot_fill, fill_val);
}

rlk_wg_stage_t
rlk_wg_drain(rlk_wg_drain_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->drain && cfg->elev);
    assert(rlk_lattice_same(&cfg->drain->lattice, &cfg->elev->lattice));
    if (cfg->acc_scale == 0.0f)
        cfg->acc_scale = 1.0f;
    return (rlk_wg_stage_t){ .fn = drain_stage, .data = cfg };
}
