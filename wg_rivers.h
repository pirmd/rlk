#ifndef RLK_WG_RIVERS_H
#define RLK_WG_RIVERS_H

/*
 * Optional wg stage: project a pre-built rlk_drain_t drainage network
 * into per-tile river masks and valley carves.
 *
 * The network is queried as a signed distance field (rlk_drain_river_sdf,
 * constant cost per tile): < 0 in the channel, 0 on the bank, > 0 outside.
 * The channel width follows hydraulic geometry inside the drain module
 * (~ log2(acc), capped at cellsize tiles wide). The river mask is 1 in the
 * channel, fading to 0 over `bank_fade` tiles past the bank. The valley
 * carve is a quadratic profile with reach = valley_mult * cellsize.
 *
 * Hack's law (specialist review 2026-10-30, hydrologist): with acc_log_field
 * set, the effective bank fade grows with the normalized log accumulation —
 * bank_fade * (1 + acc_log_gain * acc_log) — so big rivers also have wide
 * banks. acc_log_field typically reads the slot written by wg_drain's
 * slot_acc_log; the stage order is validated by rlk_wg_init().
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
 * The drain and elevation grid must be built before rlk_wg_init() and must
 * outlive the wg.
 *
 * Stage order is validated by rlk_wg_init(): a consumer of slot_carve
 * (e.g. wg_relief via river_carve_field) must be registered AFTER this
 * stage, otherwise rlk_wg_init() returns RLK_E_INVALID.
 */

#include "wg.h"
#include "drain.h"
#include "grid.h"

#define RLK_WG_RIVERS_NONE 255  /* slot value: do not write this output */

/* Rivers stage configuration. Const after construction: zero defaults are
   resolved inside the stage, the cfg is never mutated. */
typedef struct {
    const rlk_drain_t *drain; /* pre-built drainage; must outlive the wg */
    const rlk_grid_t *elev;   /* elevation grid matching drain; must outlive the wg */
    float sea;                /* sea level (rivers only on land) */

    uint32_t acc_min;           /* min drainage area (cells) for a river segment */
    float bank_fade;            /* mask fade width past the bank, tiles (0 -> 1.0) */
    float valley_mult;          /* valley reach = valley_mult * cellsize (>= 1) */
    float carve;                /* valley depth at centre, elevation units (>= 0) */

    uint8_t slot_mask;          /* derived field for river mask [0,1] */
    uint8_t slot_carve;         /* derived field for valley carve, or NONE */

    /* Optional: Hack's law — widen the bank fade with log(acc).
       Off (no slot read) when acc_log_field is NONE or acc_log_gain <= 0,
       so a zero-initialized cfg stays valid. */
    uint8_t acc_log_field;      /* derived field with normalized log10(acc), or NONE */
    float   acc_log_gain;       /* widening gain (e.g. 0.3); <= 0 = off */

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
} rlk_wg_rivers_cfg_t;

/* Build a stage from a const config. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_rivers(const rlk_wg_rivers_cfg_t *cfg);

#endif /* RLK_WG_RIVERS_H */
