#ifndef RLK_WG_RELIEF_H
#define RLK_WG_RELIEF_H

/*
 * Optional wg stage: the standardised elevation field that zones classify
 * on (domain warp; continent + hills + mountains; fjords; lapse; external
 * river carve subtracted). Reads sampled fields, writes the slot_elev
 * derived field. If river_carve_field is set, register AFTER the stage
 * writing it (contract, not checked).
 */

#include "wg.h"

/* Relief stage configuration; treat as const after construction. */
typedef struct {
    /* --- Inputs: sampled fields --- */
    uint8_t f_cont;       /* sampled field for the continent (required, low freq ~0.004) */
    uint8_t f_hills;      /* sampled field for rolling relief, or RLK_WG_NONE = flat */
    uint8_t f_ridge;      /* sampled field for mountain ridges (RIDGE kind), or RLK_WG_NONE = no mountains */
    uint8_t f_warp_x;     /* sampled field for domain warp X, or RLK_WG_NONE = no warp */
    uint8_t f_warp_y;     /* sampled field for domain warp Y (must pair with f_warp_x) */
    uint8_t f_fjord;      /* sampled field for fjord noise, or RLK_WG_NONE = no fjords */
    uint8_t f_temp;       /* sampled field for temperature, or RLK_WG_NONE = no cooling, no cold fjords */

    /* --- Output: derived slot (absolute index = nsampled + slot) --- */
    uint8_t slot_elev;    /* derived field for the final elevation (required;
                             unclamped: may exceed [0,1] and drop below sea
                             after carves — writers clamp as needed) */

    /* --- Input: derived slot written by an earlier stage (e.g. wg_rivers) --- */
    uint8_t river_carve_field; /* derived field for the valley carve, or RLK_WG_NONE */

    /* --- Relief parameters; a zero/inverted value resolves to the default
           shown (resolved in place by the constructor) --- */
    float sea;            /* sea level in elevation units */
    float warp;           /* domain warp amplitude, tiles; 0 -> 30 */
    float hills_amp;      /* rolling relief amplitude; 0 -> 0.30 */
    float mountain_lo;    /* mountains fade in between these cont values;
                             hi <= lo -> lo/hi = 0.45/0.65 */
    float mountain_hi;
    float mountain_amp;   /* crest height; 0 -> 0.45 */
    float lapse;          /* temperature drop per unit height above sea; 0 -> 0.35 */

    /* --- Fjords --- */
    float fjord_dir[2];   /* unit vector: direction fjords run along */
    float fjord_stretch;  /* <1 elongates along fjord_dir; 0 -> 0.25 */
    float fjord_width;    /* width in ridge units; <= 0 -> 0.05 */
    float fjord_depth;    /* carve depth (must exceed coastal land height); 0 -> 0.45 */
    float fjord_steep;    /* profile: 1 = V, larger = U with steeper walls; 0 -> 3.0 */
    float fjord_reach;    /* only within this much cont above sea; <= 0 -> 0.12 */
    float fjord_cold0;    /* fully active below temp = cold0;
                             cold1 <= cold0 -> cold0/cold1 = 0.35/0.55 */
    float fjord_cold1;    /* off above temp = cold1 */
} rlk_wg_relief_cfg_t;

/* Build the relief stage. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_relief(rlk_wg_relief_cfg_t *cfg);

#endif /* RLK_WG_RELIEF_H */
