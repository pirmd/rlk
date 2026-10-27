#ifndef RLK_WG_NF_GRID_H
#define RLK_WG_NF_GRID_H

/*
 * Optional wg_nf stage: samples a pre-filled rlk_grid_t into a derived field.
 *
 * Applied as: ctx->field[nsampled + slot] = grid_sample(wx, wy) * scale + offset
 * The grid must be filled before rlk_wg_nf_init() and must outlive the wg.
 */

#include "wg_nf.h"
#include "grid.h"

/* Grid sampler stage configuration. */
typedef struct {
    const rlk_grid_t *grid;  /* pre-filled grid; must outlive the wg */
    uint8_t slot;               /* derived field index: ctx->field[nsampled + slot] */
    float scale;                /* 0 -> 1.0 */
    float offset;               /* added after scale */
} rlk_wg_nf_grid_t;

/* Stage function (use directly with rlk_wg_nf_add_stage if managing stages by hand). */
void rlk_wg_nf_grid_stage(rlk_wg_nf_ctx_t *ctx, const void *data);

/* Resolve zero defaults: scale 0->1.0. Call once after filling. */
void rlk_wg_nf_grid_init(rlk_wg_nf_grid_t *g);

/* Init g, then register it as a stage of wg. g must outlive the wg. */
rlk_err_t rlk_wg_nf_grid_add(rlk_wg_nf_t *wg, rlk_wg_nf_grid_t *g);

#endif /* RLK_WG_NF_GRID_H */
