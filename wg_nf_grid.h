#ifndef RLK_WG_NF_GRID_H
#define RLK_WG_NF_GRID_H

/* layer: 3, domain: worldgen */

/*
 * Optional wg_nf stage: samples a pre-filled rlk_grid_t into a derived field.
 *
 * Applied as: ctx->field[nsampled + slot] = grid_sample(wx, wy) * scale + offset
 * The grid must be filled before rlk_wg_nf_init() and must outlive the wg.
 */

#include "wg_nf.h"
#include "grid.h"

/* Grid sampler stage configuration. Const after construction: scale 0 -> 1.0
   is resolved inside the stage, the cfg is never mutated. */
typedef struct {
    const rlk_grid_t *grid;  /* pre-filled grid; must outlive the wg */
    uint8_t slot;               /* derived field index: ctx->field[nsampled + slot] */
    float scale;                /* 0 -> 1.0 */
    float offset;               /* added after scale */
} rlk_wg_nf_grid_cfg_t;

/* Build a stage from a const config. The cfg must outlive the wg. */
rlk_wg_nf_stage_t rlk_wg_nf_grid(const rlk_wg_nf_grid_cfg_t *cfg);

#endif /* RLK_WG_NF_GRID_H */
