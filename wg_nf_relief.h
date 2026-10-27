#ifndef RLK_WG_NF_RELIEF_H
#define RLK_WG_NF_RELIEF_H

/*
 * Optional wg_nf stage: compute a standardised elevation field from sampled
 * noise fields, with domain warp, mountains, rivers, fjords and altitudinal
 * cooling.
 *
 * Pipeline per tile:
 *   1. Domain warp: displace (wx,wy) using two warp fields
 *   2. Base elevation: cont + hills_amp*(hills-0.5) + mountain_amp*smoothstep*ridge
 *   3. Rivers: either noise-based (level line of f_river field) or from a
 *      pre-computed derived field (e.g. produced by wg_nf_rivers)
 *   4. Fjords: straight, deep valleys near cold coasts
 *   5. Subtract river and fjord carves
 *   6. Altitudinal cooling: temp -= lapse * (elev - sea)
 *
 * Outputs (derived fields):
 *   slot_elev  -> final elevation [~0,1], after carving
 *   slot_river -> river mask [0,1] (optional)
 *
 * Field indices refer to ctx->field[]: sampled fields are [0, nsampled),
 * derived fields are [nsampled, nsampled+nderived). Use RLK_WG_NF_RELIEF_NONE
 * (255) to disable an optional input.
 *
 * STAGE ORDER: if river_carve_field is set (e.g. from wg_nf_rivers), this stage
 * must run AFTER wg_nf_rivers so the carve is already computed. Adding stages
 * in the wrong order silently skips river valley carving.
 */

#include "wg_nf.h"

#define RLK_WG_NF_RELIEF_NONE 255  /* field index: disabled */

/* Smallest noise slope used to turn a noise difference into a distance. */
#define RLK_WG_NF_RELIEF_MIN_GRAD 0.004f

typedef struct {
    /* --- Sampled field indices (0..nsampled-1, or NONE) --- */
    uint8_t f_cont;       /* continent field (required, low freq ~0.004) */
    uint8_t f_hills;      /* rolling relief (NONE = flat) */
    uint8_t f_ridge;      /* mountain ridges, RIDGE kind (NONE = no mountains) */
    uint8_t f_warp_x;     /* domain warp X (NONE = no warp) */
    uint8_t f_warp_y;     /* domain warp Y (must pair with f_warp_x) */
    uint8_t f_fjord;      /* fjord noise field (NONE = no fjords) */
    uint8_t f_temp;       /* temperature (NONE = no cooling, no cold fjords) */
    uint8_t f_river;      /* river noise field (NONE = no noise rivers) */

    /* --- Derived field slots (0..nderived-1, or NONE) --- */
    uint8_t slot_elev;    /* output: final elevation */
    uint8_t slot_river;   /* output: river mask [0,1] (NONE = skip) */

    /* --- Derived field input (optional, e.g. from wg_nf_rivers) --- */
    uint8_t river_carve_field; /* derived field with valley carve (NONE = use noise) */

    /* --- Relief parameters --- */
    float sea;            /* sea level in elevation units */
    float warp;           /* domain warp amplitude, tiles */
    float hills_amp;      /* rolling relief amplitude */
    float mountain_lo;    /* mountains fade in between these cont values */
    float mountain_hi;
    float mountain_amp;   /* crest height */
    float lapse;          /* temperature drop per unit height above sea */

    /* --- Noise rivers --- */
    float river_width_tiles; /* water half-width at coast, tiles */
    float river_carve;       /* valley depth, elevation units */
    float relief_span;       /* height above sea where rivers are thinnest */

    /* --- Fjords --- */
    float fjord_dir[2];   /* unit vector: direction fjords run along */
    float fjord_stretch;  /* <1 elongates along fjord_dir */
    float fjord_width;    /* width in ridge units */
    float fjord_depth;    /* carve depth (must exceed coastal land height) */
    float fjord_steep;    /* profile: 1 = V, larger = U with steeper walls */
    float fjord_reach;    /* only within this much cont above sea */
    float fjord_cold0;    /* fully active below temp = cold0 */
    float fjord_cold1;    /* off above temp = cold1 */
} rlk_wg_nf_relief_t;

/* Stage function (use directly with rlk_wg_nf_add_stage if managing stages by hand). */
void rlk_wg_nf_relief_stage(rlk_wg_nf_ctx_t *ctx, const void *data);

/* Resolve zero defaults. Call once after filling. */
void rlk_wg_nf_relief_init(rlk_wg_nf_relief_t *r);

/* Init r, then register it as a stage of wg. r must outlive the wg. */
rlk_err_t rlk_wg_nf_relief_add(rlk_wg_nf_t *wg, rlk_wg_nf_relief_t *r);

#endif /* RLK_WG_NF_RELIEF_H */
