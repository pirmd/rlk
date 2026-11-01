#include "wg_hydro.h"

#include <assert.h>

static void
hydro_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_hydro_cfg_t *cfg = (const rlk_wg_hydro_cfg_t *)data;
    const rlk_wg_t *wg = ctx->wg;

    /* Slot bounds are checked here (the constructor does not know the wg). */
    assert(cfg->slot_slope    == RLK_WG_NONE || wg->nsampled + (int)cfg->slot_slope    < RLK_WG_MAX_FIELDS);
    assert(cfg->slot_strahler == RLK_WG_NONE || wg->nsampled + (int)cfg->slot_strahler < RLK_WG_MAX_FIELDS);
    assert(cfg->slot_speed    == RLK_WG_NONE || wg->nsampled + (int)cfg->slot_speed    < RLK_WG_MAX_FIELDS);

    const rlk_hydro_t *h = cfg->hydro;
    const int c = rlk_lattice_index(&h->lattice, ctx->wx, ctx->wy);

    float slope = 0.0f, strahler = 0.0f, speed = 0.0f;
    if (c >= 0) {
        const size_t k = (size_t)c;
        if (h->slope)    slope    = h->slope[k];
        if (h->strahler) strahler = (float)h->strahler[k];
        if (h->speed)    speed    = h->speed[k];
    }

    if (cfg->slot_slope != RLK_WG_NONE)
        ctx->field[wg->nsampled + cfg->slot_slope] = slope;
    if (cfg->slot_strahler != RLK_WG_NONE)
        ctx->field[wg->nsampled + cfg->slot_strahler] = strahler;
    if (cfg->slot_speed != RLK_WG_NONE)
        ctx->field[wg->nsampled + cfg->slot_speed] = speed;
}

rlk_wg_stage_t
rlk_wg_hydro(rlk_wg_hydro_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->hydro);
    return (rlk_wg_stage_t){ .fn = hydro_stage, .data = cfg };
}
