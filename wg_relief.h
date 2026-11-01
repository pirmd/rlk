#ifndef RLK_WG_RELIEF_H
#define RLK_WG_RELIEF_H

/*
 * Optional wg stage: the standardised elevation field that zones classify
 * on (domain warp, continent + hills + mountains, fjords, lapse; external
 * river carve subtracted). Field indices refer to ctx->field[]: sampled
 * are [0, nsampled), derived [nsampled, ..); RLK_WG_NONE disables an
 * input. If river_carve_field is set, register AFTER the stage writing it
 * (contract, not checked).
 */

#include "wg.h"

/* Relief stage configuration; treat as const after construction. */
typedef struct {
    /* --- Sampled field indices (0..nsampled-1, or RLK_WG_NONE) --- */
    uint8_t f_cont;       /* continent field (required, low freq ~0.004) */
    uint8_t f_hills;      /* rolling relief (RLK_WG_NONE = flat) */
    uint8_t f_ridge;      /* mountain ridges, RIDGE kind (RLK_WG_NONE = no mountains) */
    uint8_t f_warp_x;     /* domain warp X (RLK_WG_NONE = no warp) */
    uint8_t f_warp_y;     /* domain warp Y (must pair with f_warp_x) */
    uint8_t f_fjord;      /* fjord noise field (RLK_WG_NONE = no fjords) */
    uint8_t f_temp;       /* temperature (RLK_WG_NONE = no cooling, no cold fjords) */

    /* --- Derived field slot (0..nderived-1) --- */
    uint8_t slot_elev;    /* output: final elevation */

    /* --- Derived field input (optional, e.g. from wg_rivers) --- */
    uint8_t river_carve_field; /* derived field with valley carve, or RLK_WG_NONE */

    /* --- Relief parameters --- */
    float sea;            /* sea level in elevation units */
    float warp;           /* domain warp amplitude, tiles */
    float hills_amp;      /* rolling relief amplitude */
    float mountain_lo;    /* mountains fade in between these cont values */
    float mountain_hi;
    float mountain_amp;   /* crest height */
    float lapse;          /* temperature drop per unit height above sea */

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

/* Build the relief stage; zero defaults are resolved in place.
   The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_relief(rlk_wg_relief_cfg_t *cfg);

#endif /* RLK_WG_RELIEF_H */
