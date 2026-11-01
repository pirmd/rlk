#ifndef RLK_WG_DRAIN_H
#define RLK_WG_DRAIN_H

/*
 * Optional wg stage: reader for a pre-built rlk_drain_t — projects the
 * drainage network (acc, downstream dir, lake depth) into per-tile
 * derived slots for stages and writers. The drain and elevation grid
 * must be built before rlk_wg_init() and must outlive the wg.
 */

#include "wg.h"
#include "drain.h"
#include "grid.h"

/* Drain reader stage configuration. The constructor resolves the zero
   default in place (acc_scale 0 -> 1.0); treat the cfg as const afterwards. */
typedef struct {
    const rlk_drain_t *drain;  /* pre-built drain; must outlive the wg */
    const rlk_grid_t *elev;    /* elevation grid (for lake depth); must outlive the wg */
    float sea;                 /* sea level for lake detection */
    uint8_t slot_acc;          /* derived field for accumulation, or RLK_WG_NONE */
    uint8_t slot_dir;          /* derived field for direction, or RLK_WG_NONE */
    uint8_t slot_lake;         /* derived field for lake depth, or RLK_WG_NONE */
    float acc_scale;           /* accumulation divisor; 0 -> 1.0 */
} rlk_wg_drain_cfg_t;

/* Build a stage that reads the pre-built drain at each tile and writes the
   configured derived slots (flow acc, downstream dir, lake depth;
   RLK_WG_NONE skips a slot). The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_drain(rlk_wg_drain_cfg_t *cfg);

#endif /* RLK_WG_DRAIN_H */
