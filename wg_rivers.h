#ifndef RLK_WG_RIVERS_H
#define RLK_WG_RIVERS_H

/*
 * Optional wg stage: the water layer — projects a pre-built rlk_drain_t
 * into a per-tile river mask and valley carve (SDF projection; optional
 * log(acc) bank widening, meanders, creeks, lake overlay — see the cfg
 * fields). Build the drain and elevation grid after rlk_wg_init() (e.g.
 * rlk_wg_fill_grid + rlk_drain_build) and before the first rlk_wg_fill().
 *
 * Stage order (contract, not checked): a consumer of the carve slot (e.g.
 * wg_relief via river_carve_field) must be registered AFTER this stage.
 */

#include "wg.h"
#include "drain.h"
#include "grid.h"

/* Rivers stage configuration; treat as const after construction. */
typedef struct {
    /* --- Macro inputs: pre-built PODs (build after rlk_wg_init, see banner) --- */
    const rlk_drain_t *drain;      /* pre-built drainage */
    const rlk_grid_t  *elev;       /* elevation grid matching the drain lattice */
    const rlk_grid_t  *lake_depth; /* lake depth grid; NULL = no lake overlay */

    /* --- Outputs: derived slots (absolute index = nsampled + slot) --- */
    uint8_t slot_mask;         /* derived field for the river mask in [0,1], or RLK_WG_NONE */
    uint8_t slot_carve;        /* derived field for the valley carve, or RLK_WG_NONE */

    /* --- Inputs: sampled fields (optional) --- */
    uint8_t f_wig_x;           /* sampled field for meander X displacement, or RLK_WG_NONE = no meanders */
    uint8_t f_wig_y;           /* sampled field for meander Y displacement (must pair with f_wig_x) */
    uint8_t f_creek;           /* sampled field for creek noise, or RLK_WG_NONE = no creeks */

    /* --- Parameters; a zero/inverted value resolves to the default shown
           (resolved in place by the constructor) --- */
    float sea;                 /* sea level in elevation units (rivers clipped below it) */
    uint32_t acc_min;          /* min drainage area in cells for a river segment; 0 -> 1 */
    float bank_fade;           /* mask fade width past the bank, tiles; <= 0 -> 1.0 */
    float valley_mult;         /* valley reach = valley_mult * cellsize; < 1 -> 1.0 */
    float carve;               /* valley depth at centre, elevation units */
    float wig_tiles;           /* meander amplitude, tiles; <= 0 = off */
    float creek_width;         /* creek half-width, tiles; <= 0 = off */
    float creek_carve;         /* creek valley depth, elevation units */
    float acc_log_gain;        /* log-width law: bank fade widened by gain * log10(acc); <= 0 = off */
    float lake_min_depth;      /* min lake depth for the overlay, elevation units; <= 0 = off */
} rlk_wg_rivers_cfg_t;

/* Build the rivers stage. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_rivers(rlk_wg_rivers_cfg_t *cfg);

#endif /* RLK_WG_RIVERS_H */
