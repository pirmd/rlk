#ifndef RLK_WG_HYDRO_H
#define RLK_WG_HYDRO_H

/*
 * Optional wg stage: reads a pre-built rlk_hydro_t into derived fields.
 *
 * Writes up to three derived fields per tile (nearest grid cell of the
 * tile position; 0 outside the grid):
 *   slot_slope    -> D8 downslope, elevation units per tile
 *   slot_strahler -> Strahler stream order (as a float, 1..)
 *   slot_speed    -> Manning speed estimate, relative units
 * A slot of RLK_WG_HYDRO_NONE disables that output.
 * The hydro must be built before rlk_wg_init() and must outlive the wg.
 */

#include "wg.h"
#include "hydro.h"

#define RLK_WG_HYDRO_NONE 255  /* slot value: do not write this output */

/* Hydro reader stage configuration. Const after construction. */
typedef struct {
    const rlk_hydro_t *hydro;  /* pre-built hydro; must outlive the wg */
    uint8_t slot_slope;        /* derived field for slope, or NONE */
    uint8_t slot_strahler;     /* derived field for Strahler order, or NONE */
    uint8_t slot_speed;        /* derived field for speed, or NONE */
} rlk_wg_hydro_cfg_t;

/* Build a stage from a const config. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_hydro(const rlk_wg_hydro_cfg_t *cfg);

#endif /* RLK_WG_HYDRO_H */
