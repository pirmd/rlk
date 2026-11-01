#ifndef RLK_WG_DRAIN_H
#define RLK_WG_DRAIN_H

/*
 * Optional wg stage: reader for a pre-built rlk_drain_t — projects the
 * drainage network (acc, downstream dir, lake depth) into per-tile
 * derived slots for stages and writers. Build the drain and elevation grid
 * after rlk_wg_init() (e.g. rlk_wg_fill_grid + rlk_drain_build) and before
 * the first rlk_wg_fill().
 */

#include "wg.h"
#include "drain.h"
#include "grid.h"

/* Drain reader stage configuration; treat as const after construction. */
typedef struct {
    const rlk_drain_t *drain;  /* pre-built drain */
    const rlk_grid_t *elev;    /* elevation grid (for lake depth) */
    float sea;                 /* sea level for lake detection */
    uint8_t slot_acc;          /* derived field for accumulation, or RLK_WG_NONE */
    uint8_t slot_dir;          /* derived field for direction, or RLK_WG_NONE */
    uint8_t slot_lake;         /* derived field for lake depth, or RLK_WG_NONE */
    float acc_scale;           /* accumulation divisor; 0 -> 1.0 */
} rlk_wg_drain_cfg_t;

/* Build the drain reader stage; the zero default (acc_scale 0 -> 1.0) is
   resolved in place. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_drain(rlk_wg_drain_cfg_t *cfg);

#endif /* RLK_WG_DRAIN_H */
