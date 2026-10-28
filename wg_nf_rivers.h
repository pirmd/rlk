#ifndef RLK_WG_NF_RIVERS_H
#define RLK_WG_NF_RIVERS_H

/*
 * Optional wg_nf stage: project a pre-built rlk_drain_t drainage network
 * into per-tile river masks and valley carves.
 *
 * For each tile, scans nearby drain cells with accumulation >= acc_min and
 * computes the distance to the drainage segment (cell centre -> downstream
 * cell centre). The river mask is 1 at the segment, fading to 0 at `width`
 * tiles away. Width follows hydraulic geometry: ~ sqrt(acc), capped.
 *
 * Outputs (derived fields):
 *   slot_mask  -> river mask [0,1] (0 = no river, 1 = river centre)
 *   slot_carve -> valley depth to subtract from elevation (optional)
 *
 * Lakes (optional): a pre-computed lake-depth grid (same geometry as elev)
 * adds a smooth lake mask on top of the river mask.
 *
 * Meanders (optional): two decorrelated noise fields (f_wig_x, f_wig_y)
 * displace the tile position before projection onto drain segments, turning
 * straight cell-centre polylines into believable meandering rivers.
 *
 * Creeks (optional): a noise field (f_creek) adds small headwater streams
 * on top of the drainage network, as level lines of the noise field.
 *
 * The drain and elevation grid must be built before rlk_wg_nf_init() and must
 * outlive the wg.
 *
 * STAGE ORDER: this stage must run BEFORE wg_nf_relief (which reads slot_carve
 * via river_carve_field). Adding stages in the wrong order silently produces
 * rivers without valley carving.
 */

#include "wg_nf.h"
#include "drain.h"
#include "grid.h"

#define RLK_WG_NF_RIVERS_NONE 255  /* slot value: do not write this output */

typedef struct {
    const rlk_drain_t *drain; /* pre-built drainage; must outlive the wg */
    const rlk_grid_t *elev;   /* elevation grid matching drain; must outlive the wg */
    float sea;                /* sea level (rivers only on land) */

    uint32_t acc_min;           /* min drainage area (cells) for a river segment */
    float width_min;            /* water half-width at acc_min, tiles */
    float width_max;            /* cap on half-width, tiles */
    float valley_mult;          /* valley half-width = valley_mult * width (>= 1) */
    float carve;                /* valley depth at centre, elevation units (>= 0) */

    uint8_t slot_mask;          /* derived field for river mask [0,1] */
    uint8_t slot_carve;         /* derived field for valley carve, or NONE */

    /* Optional: lake overlay */
    const rlk_grid_t *lake_depth;  /* pre-computed lake depths; NULL = none */
    float lake_min_depth;             /* min depth for lake mask (elevation units) */

    /* Optional: meanders (fine displacement of the tile position) */
    uint8_t f_wig_x;            /* sampled field for meander X displacement, or NONE */
    uint8_t f_wig_y;            /* sampled field for meander Y displacement, or NONE */
    float   wig_tiles;          /* meander amplitude in tiles (e.g. 3.0) */

    /* Optional: creeks (noise level-line streams on top of the drainage) */
    uint8_t f_creek;            /* sampled noise field for creeks, or NONE */
    float   creek_width;        /* creek half-width in tiles */
    float   creek_carve;        /* creek valley depth, elevation units */
} rlk_wg_nf_rivers_t;

/* Stage function (use directly with rlk_wg_nf_add_stage if managing stages by hand). */
void rlk_wg_nf_rivers_stage(rlk_wg_nf_ctx_t *ctx, const void *data);

/* Resolve zero defaults. Call once after filling. */
void rlk_wg_nf_rivers_init(rlk_wg_nf_rivers_t *r);

/* Init r, then register it as a stage of wg. r must outlive the wg. */
rlk_err_t rlk_wg_nf_rivers_add(rlk_wg_nf_t *wg, rlk_wg_nf_rivers_t *r);

#endif /* RLK_WG_NF_RIVERS_H */
