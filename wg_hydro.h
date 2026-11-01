#ifndef RLK_WG_HYDRO_H
#define RLK_WG_HYDRO_H

/*
 * Optional wg stage: reader for a pre-built rlk_hydro_t — projects the
 * hydraulic per-cell data (slope, Strahler order, speed) into per-tile
 * derived slots. Build the chain after rlk_wg_init() and before the first
 * rlk_wg_fill(): rlk_wg_fill_grid + rlk_drain_build, then rlk_hydro_build.
 * The slots serve ctx-only consumers (zone writers, axes); a stage that
 * already holds the rlk_hydro_t pointer reads it directly and leaves the
 * slot RLK_WG_NONE. Tiles outside the lattice, and NULL hydro buffers,
 * read as 0.
 */

#include "wg.h"
#include "hydro.h"

/* Hydro reader stage configuration; treat as const after construction. */
typedef struct {
    const rlk_hydro_t *hydro;  /* pre-built hydro */
    uint8_t slot_slope;        /* derived field for slope (elevation units per
                                  tile, >= 0), or RLK_WG_NONE */
    uint8_t slot_strahler;     /* derived field for Strahler order (>= 1),
                                  or RLK_WG_NONE */
    uint8_t slot_speed;        /* derived field for Manning speed (relative
                                  units, >= 0), or RLK_WG_NONE */
} rlk_wg_hydro_cfg_t;

/* Build the hydro reader stage. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_hydro(rlk_wg_hydro_cfg_t *cfg);

#endif /* RLK_WG_HYDRO_H */
