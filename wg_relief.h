#ifndef RLK_WG_RELIEF_H
#define RLK_WG_RELIEF_H

/*
 * Optional wg stage: compute a standardised elevation field from sampled
 * noise fields, with domain warp, mountains, rivers, fjords and altitudinal
 * cooling.
 *
 * Pipeline per tile:
 *   1. Domain warp: displace (wx,wy) using two warp fields
 *   2. Base elevation: cont + hills_amp*(hills-0.5) + mountain_amp*smoothstep*ridge
 *   3. Rivers: either noise-based (level line of f_river field) or from a
 *      pre-computed derived field (e.g. produced by wg_rivers)
 *   4. Fjords: straight, deep valleys near cold coasts
 *   5. Subtract river and fjord carves
 *   6. Altitudinal cooling: temp -= lapse * (elev - sea)
 *
 * Outputs (derived fields):
 *   slot_elev  -> final elevation [~0,1], after carving
 *   slot_river -> river mask [0,1] (optional)
 *
 * Field indices refer to ctx->field[]: sampled fields are [0, nsampled),
 * derived fields are [nsampled, nsampled+nderived). Use RLK_WG_RELIEF_NONE
 * (255) to disable an optional input.
 *
 * If river_carve_field is set (e.g. from wg_rivers), this stage must be
 * registered AFTER the stage writing that slot: rlk_wg_init() validates the
 * order and returns RLK_E_INVALID otherwise.
 */

#include "wg.h"

#define RLK_WG_RELIEF_NONE 255  /* field index: disabled */

/* Relief stage configuration. Const after construction: zero defaults are
   resolved inside the stage, the cfg is never mutated. */
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

    /* --- Derived field input (optional, e.g. from wg_rivers) --- */
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
} rlk_wg_relief_cfg_t;

/* Build a stage from a const config. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_relief(const rlk_wg_relief_cfg_t *cfg);

#endif /* RLK_WG_RELIEF_H */
