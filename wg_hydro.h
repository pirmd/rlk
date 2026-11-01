#ifndef RLK_WG_HYDRO_H
#define RLK_WG_HYDRO_H

/*
 * Optional wg stage: reader for a pre-built rlk_hydro_t — projects the
 * hydraulic per-cell data (slope, Strahler order, speed) into derived
 * slots. Build the hydro after rlk_wg_init() (rlk_hydro_build on a drain
 * built the same way) and before the first rlk_wg_fill().
 */

#include "wg.h"
#include "hydro.h"

/* Hydro reader stage configuration; treat as const after construction. */
typedef struct {
    const rlk_hydro_t *hydro;  /* pre-built hydro */
    uint8_t slot_slope;        /* derived field for slope, or RLK_WG_NONE */
    uint8_t slot_strahler;     /* derived field for Strahler order, or RLK_WG_NONE */
    uint8_t slot_speed;        /* derived field for speed, or RLK_WG_NONE */
} rlk_wg_hydro_cfg_t;

/* Build the hydro reader stage (no zero defaults). The cfg must outlive
   the wg. */
rlk_wg_stage_t rlk_wg_hydro(rlk_wg_hydro_cfg_t *cfg);

#endif /* RLK_WG_HYDRO_H */
