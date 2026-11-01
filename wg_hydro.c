#include "wg_hydro.h"

#include <assert.h>

/* Write v into derived slot `slot` (RLK_WG_NONE = skip). */
static void
set_derived(rlk_wg_ctx_t *ctx, uint8_t slot, float v) {
    if (slot == RLK_WG_NONE)
        return;
    const int idx = ctx->wg->nsampled + slot;
    assert(idx < ctx->wg->nsampled + ctx->wg->nderived);
    ctx->field[idx] = v;
}

static void
hydro_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_hydro_cfg_t *f = (const rlk_wg_hydro_cfg_t *)data;

    const rlk_hydro_t *h = f->hydro;
    const int c = rlk_lattice_index(&h->lattice, ctx->wx, ctx->wy);

    float slope = 0.0f, strahler = 0.0f, speed = 0.0f;
    if (c >= 0) {
        const size_t k = (size_t)c;
        if (h->slope)    slope    = h->slope[k];
        if (h->strahler) strahler = (float)h->strahler[k];
        if (h->speed)    speed    = h->speed[k];
    }

    set_derived(ctx, f->slot_slope, slope);
    set_derived(ctx, f->slot_strahler, strahler);
    set_derived(ctx, f->slot_speed, speed);
}

rlk_wg_stage_t
rlk_wg_hydro(rlk_wg_hydro_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->hydro);
    return (rlk_wg_stage_t){ .fn = hydro_stage, .data = cfg };
}
