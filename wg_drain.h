#ifndef RLK_WG_DRAIN_H
#define RLK_WG_DRAIN_H

/*
 * Optional wg stage: reader for a pre-built rlk_drain_t — projects the
 * drainage network (acc, downstream dir, fill depth) into per-tile derived
 * slots. Build the drain and elevation grid after rlk_wg_init() (e.g.
 * rlk_wg_fill_grid + rlk_drain_build) and before the first rlk_wg_fill().
 * The slots serve ctx-only consumers (zone writers, axes); a stage that
 * already holds the rlk_drain_t pointer (e.g. wg_rivers, wg_poi) reads it
 * directly and leaves the slot RLK_WG_NONE.
 */

#include "wg.h"
#include "drain.h"
#include "grid.h"

/* Drain reader stage configuration; treat as const after construction. */
typedef struct {
    const rlk_drain_t *drain;  /* pre-built drain */
    const rlk_grid_t  *elev;   /* elevation grid (for fill depth) */
    uint8_t slot_acc;          /* derived field for accumulation in [0,1]
                                  (raw cell count / lattice cells), or RLK_WG_NONE */
    uint8_t slot_dir;          /* derived field for downstream direction (raw D8 code,
                                  RLK_DRAIN_OUTLET at the border), or RLK_WG_NONE */
    uint8_t slot_fill;         /* derived field for fill depth, or RLK_WG_NONE */
} rlk_wg_drain_cfg_t;

/* Build the drain reader stage. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_drain(rlk_wg_drain_cfg_t *cfg);

#endif /* RLK_WG_DRAIN_H */
