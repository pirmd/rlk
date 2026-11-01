#ifndef RLK_WG_RIVERS_H
#define RLK_WG_RIVERS_H

/*
 * Optional wg stage: the water layer — projects a pre-built rlk_drain_t
 * into a per-tile river mask and valley carve (SDF projection; optional
 * log(acc) bank widening, meanders, creeks, lake overlay — see the cfg
 * fields). The drain and elevation grid must be built before rlk_wg_init()
 * and must outlive the wg.
 *
 * Stage order (contract, not checked): a consumer of the carve slot (e.g.
 * wg_relief via river_carve_field) must be registered AFTER this stage.
 */

#include "wg.h"
#include "drain.h"
#include "grid.h"

/* Rivers stage configuration. The constructor resolves the zero defaults
   in place; treat the cfg as const afterwards. */
typedef struct {
    const rlk_drain_t *drain; /* pre-built drainage; must outlive the wg */
    const rlk_grid_t *elev;   /* elevation grid matching drain; must outlive the wg */
    float sea;                /* sea level (rivers only on land) */

    uint32_t acc_min;           /* min drainage area (cells) for a river segment */
    float bank_fade;            /* mask fade width past the bank, tiles (<= 0 -> 1.0) */
    float valley_mult;          /* valley reach = valley_mult * cellsize (>= 1) */
    float carve;                /* valley depth at centre, elevation units (>= 0) */

    uint8_t slot_mask;          /* derived field for river mask [0,1] */
    uint8_t slot_carve;         /* derived field for valley carve, or RLK_WG_NONE */

    /* Optional: log-width law — widen the bank fade with log10(acc), read
       from the drain. Off when acc_log_gain <= 0, so a zero-initialized
       cfg stays valid. */
    float acc_log_gain;         /* widening gain (e.g. 0.3); <= 0 = off */

    /* Optional: lake overlay */
    const rlk_grid_t *lake_depth;  /* pre-computed lake depths; NULL = none */
    float lake_min_depth;          /* min depth for lake mask (elevation units) */

    /* Optional: meanders (fine displacement of the tile position) */
    uint8_t f_wig_x;            /* sampled field for meander X displacement, or RLK_WG_NONE */
    uint8_t f_wig_y;            /* sampled field for meander Y displacement, or RLK_WG_NONE */
    float   wig_tiles;          /* meander amplitude in tiles (e.g. 3.0) */

    /* Optional: creeks (noise level-line streams on top of the drainage) */
    uint8_t f_creek;            /* sampled noise field for creeks, or RLK_WG_NONE */
    float   creek_width;        /* creek half-width in tiles */
    float   creek_carve;        /* creek valley depth, elevation units */
} rlk_wg_rivers_cfg_t;

/* Build a stage that projects the drainage network at each tile into a
   river mask (bank fade, optional log(acc) widening, meanders, creeks,
   lake overlay) and a valley carve. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_rivers(rlk_wg_rivers_cfg_t *cfg);

#endif /* RLK_WG_RIVERS_H */
