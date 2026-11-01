#ifndef RLK_WG_HYDRO_H
#define RLK_WG_HYDRO_H

/*
 * Optional wg stage: reader for a pre-built rlk_hydro_t — projects the
 * hydraulic per-cell data (slope, Strahler order, speed) into derived
 * slots. The hydro must be built before rlk_wg_init() and must outlive
 * the wg.
 */

#include "wg.h"
#include "hydro.h"

/* Hydro reader stage configuration. No zero defaults; treat as const after
   construction. */
typedef struct {
    const rlk_hydro_t *hydro;  /* pre-built hydro; must outlive the wg */
    uint8_t slot_slope;        /* derived field for slope, or RLK_WG_NONE */
    uint8_t slot_strahler;     /* derived field for Strahler order, or RLK_WG_NONE */
    uint8_t slot_speed;        /* derived field for speed, or RLK_WG_NONE */
} rlk_wg_hydro_cfg_t;

/* Build a stage that reads the pre-built hydro at each tile and writes the
   configured derived slots (D8 slope, Strahler order, flow speed;
   RLK_WG_NONE skips a slot). The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_hydro(rlk_wg_hydro_cfg_t *cfg);

#endif /* RLK_WG_HYDRO_H */
