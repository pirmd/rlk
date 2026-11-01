#ifndef RLK_WG_REGION_H
#define RLK_WG_REGION_H

/*
 * Optional wg stage: world macro-regions — large Voronoi areas (arid,
 * cold, isles...). Each region type applies a constant bias/gain to every
 * sampled field: the noise keeps its local texture everywhere, but reads
 * as level plateaus per region — a climate map laid over the raw noise,
 * which the zones classified afterwards follow. `blend` only smooths the
 * transition between two regions. Pure in (seed, wx, wy):
 * chunk-independent, no state.
 */

#include "wg.h"

#define RLK_WG_REGION_MAX_TYPES 8  /* max distinct region types */

typedef struct {
    float bias[RLK_WG_MAX_FIELDS];  /* additive shift */
    float gain[RLK_WG_MAX_FIELDS];  /* scale = 1 + gain; 0 = neutral */
} rlk_wg_region_type_t;

typedef struct {
    rlk_wg_region_type_t types[RLK_WG_REGION_MAX_TYPES];
    int ntypes;    /* 1..RLK_WG_REGION_MAX_TYPES */

    /* Geometry: cell/blend are coherent via the blend default (cell/4);
       keep 0 < blend <= cell/2 so borders stay local. Large cell (default
       128) = few macro-regions; small cell = noisy borders. User's call,
       only cell > 0 is asserted. */
    int cell;      /* region (Voronoi cell) size in tiles; 0 -> 128, else > 0 */
    int blend;     /* border band in tiles: > 0 = band width, 0 -> cell/4,
                      < 0 = hard borders (no blend: nearest cell wins) */

    uint64_t salt; /* decorrelates several region layers */
} rlk_wg_region_cfg_t;

/* Build a stage giving the world macro-regions: each Voronoi cell (of
   `cell` tiles) carries a type whose bias/gain is applied to every
   sampled field, blended over `blend` tiles at region borders:
   field[i] = (field[i] + bias[i]) * (1 + gain[i]). The cfg must outlive
   the wg. */
rlk_wg_stage_t rlk_wg_region(rlk_wg_region_cfg_t *cfg);

#endif /* RLK_WG_REGION_H */
