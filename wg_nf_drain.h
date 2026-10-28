#ifndef RLK_WG_NF_DRAIN_H
#define RLK_WG_NF_DRAIN_H

/*
 * Optional wg_nf stage: reads a pre-built rlk_drain_t into derived fields.
 *
 * Writes up to three derived fields per tile:
 *   slot_acc  -> flow accumulation (normalized by acc_scale)
 *   slot_dir  -> downstream direction (0..7, RLK_DRAIN_OUTLET=8)
 *   slot_lake -> lake depth (filled minus original elevation)
 * A slot of RLK_WG_NF_DRAIN_NONE disables that output.
 * The drain and elevation grid must be built before rlk_wg_nf_init() and must
 * outlive the wg.
 */

#include "wg_nf.h"
#include "drain.h"
#include "grid.h"

#define RLK_WG_NF_DRAIN_NONE 255  /* slot value: do not write this output */

/* Drain reader stage configuration. */
typedef struct {
    const rlk_drain_t *drain;  /* pre-built drain; must outlive the wg */
    const rlk_grid_t *elev;    /* elevation grid (for lake_depth); must outlive the wg */
    float sea;                 /* sea level for lake detection */
    uint8_t slot_acc;          /* derived field for accumulation, or NONE */
    uint8_t slot_dir;          /* derived field for direction, or NONE */
    uint8_t slot_lake;         /* derived field for lake depth, or NONE */
    float acc_scale;           /* accumulation divisor; 0 -> 1.0 */
} rlk_wg_nf_drain_t;

/* Stage function (use directly with rlk_wg_nf_add_stage if managing stages by hand). */
void rlk_wg_nf_drain_stage(rlk_wg_nf_ctx_t *ctx, const void *data);

/* Resolve zero defaults: acc_scale 0->1.0. Call once after filling. */
void rlk_wg_nf_drain_init(rlk_wg_nf_drain_t *f);

/* Init f, then register it as a stage of wg. f must outlive the wg. */
rlk_err_t rlk_wg_nf_drain_add(rlk_wg_nf_t *wg, rlk_wg_nf_drain_t *f);

#endif /* RLK_WG_NF_DRAIN_H */
