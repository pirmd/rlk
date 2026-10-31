#ifndef RLK_WG_DRAIN_H
#define RLK_WG_DRAIN_H

/*
 * Optional wg stage: reads a pre-built rlk_drain_t into derived fields.
 *
 * Writes up to four derived fields per tile:
 *   slot_acc     -> flow accumulation (normalized by acc_scale)
 *   slot_dir     -> downstream direction (0..7, RLK_DRAIN_OUTLET=8)
 *   slot_lake    -> lake depth (filled minus original elevation)
 *   slot_acc_log -> log10(acc+1) / log10(w*h+1), normalized [0,1]: Hack's law
 *                   input for consumers like wg_rivers' bank fade (specialist
 *                   review 2026-10-30; written here, not in wg_hydro, because
 *                   this stage is the one reading drain->acc)
 * A slot of RLK_WG_DRAIN_NONE disables that output.
 * The drain and elevation grid must be built before rlk_wg_init() and must
 * outlive the wg.
 */

#include "wg.h"
#include "drain.h"
#include "grid.h"

#define RLK_WG_DRAIN_NONE 255  /* slot value: do not write this output */

/* Drain reader stage configuration. Const after construction: acc_scale
   0 -> 1.0 is resolved inside the stage, the cfg is never mutated. */
typedef struct {
    const rlk_drain_t *drain;  /* pre-built drain; must outlive the wg */
    const rlk_grid_t *elev;    /* elevation grid (for lake_depth); must outlive the wg */
    float sea;                 /* sea level for lake detection */
    uint8_t slot_acc;          /* derived field for accumulation, or NONE */
    uint8_t slot_dir;          /* derived field for direction, or NONE */
    uint8_t slot_lake;         /* derived field for lake depth, or NONE */
    uint8_t slot_acc_log;      /* derived field for normalized log10(acc), or NONE */
    float acc_scale;           /* accumulation divisor; 0 -> 1.0 */
} rlk_wg_drain_cfg_t;

/* Build a stage from a const config. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_drain(const rlk_wg_drain_cfg_t *cfg);

#endif /* RLK_WG_DRAIN_H */
