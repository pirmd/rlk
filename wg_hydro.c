#include "wg_hydro.h"

#include <assert.h>

static void
hydro_stage(rlk_wg_ctx_t *ctx, const void *data) {
    assert(ctx && data);
    const rlk_wg_hydro_cfg_t *f = (const rlk_wg_hydro_cfg_t *)data;
    const rlk_wg_t *wg = ctx->wg;

    const rlk_hydro_t *h = f->hydro;
    const int idx0 = rlk_lattice_index(&h->lattice, ctx->wx, ctx->wy);

    float slope = 0.0f, strahler = 0.0f, speed = 0.0f;
    if (idx0 >= 0) {
        const size_t c = (size_t)idx0;
        if (h->slope)    slope    = h->slope[c];
        if (h->strahler) strahler = (float)h->strahler[c];
        if (h->speed)    speed    = h->speed[c];
    }

    if (f->slot_slope != RLK_WG_HYDRO_NONE) {
        const int idx = wg->nsampled + f->slot_slope;
        assert(idx >= 0 && idx < RLK_WG_MAX_FIELDS);
        ctx->field[idx] = slope;
    }
    if (f->slot_strahler != RLK_WG_HYDRO_NONE) {
        const int idx = wg->nsampled + f->slot_strahler;
        assert(idx >= 0 && idx < RLK_WG_MAX_FIELDS);
        ctx->field[idx] = strahler;
    }
    if (f->slot_speed != RLK_WG_HYDRO_NONE) {
        const int idx = wg->nsampled + f->slot_speed;
        assert(idx >= 0 && idx < RLK_WG_MAX_FIELDS);
        ctx->field[idx] = speed;
    }
}

rlk_wg_stage_t
rlk_wg_hydro(const rlk_wg_hydro_cfg_t *cfg) {
    assert(cfg);
    assert(cfg->hydro);
    rlk_wg_stage_t st = { .fn = hydro_stage, .data = cfg };
    if (cfg->slot_slope != RLK_WG_HYDRO_NONE)
        st.writes.derived |= RLK_WG_SLOT(cfg->slot_slope);
    if (cfg->slot_strahler != RLK_WG_HYDRO_NONE)
        st.writes.derived |= RLK_WG_SLOT(cfg->slot_strahler);
    if (cfg->slot_speed != RLK_WG_HYDRO_NONE)
        st.writes.derived |= RLK_WG_SLOT(cfg->slot_speed);
    return st;
}
