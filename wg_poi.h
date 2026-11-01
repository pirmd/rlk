#ifndef RLK_WG_POI_H
#define RLK_WG_POI_H

/*
 * Optional wg stage: POI detector — fills a rlk_poi_list_t during the
 * fill pass; the POI list is the work queue for the second (local)
 * generation level. POI anchor = the tile, bbox = `size` x `size` around
 * it, type game-defined (the library defines none). Writes no derived
 * field.
 *
 * Usage:
 *   rlk_poi_t poi_buf[256];
 *   rlk_poi_list_t pois = { poi_buf, 0, 256 };
 *   rlk_wg_poi_cfg_t poi_cfg = {
 *       .list = &pois,
 *       .type = POI_CAVE,      // game-defined type
 *       .f_sampled = 1,        // humidity sampled field
 *       .threshold = 0.6f,
 *       .density = 0.002f,
 *       .size = 48,
 *       .drain = &drain,       // optional: near-river constraint
 *       .min_acc = 300.0f,
 *       .salt = 0xCAFE,
 *   };
 *   rlk_wg_add(&wg, rlk_wg_poi(&poi_cfg));
 *   rlk_wg_init(&wg);
 *   rlk_wg_fill(&wg, 0, 0, &tm);  // fills tm AND pois
 */

#include "wg.h"
#include "poi.h"
#include "drain.h"
#include "grid.h"

/* POI detector stage configuration; treat as const after construction. */
typedef struct {
    /* --- Output: external target (this stage writes no derived slot) --- */
    rlk_poi_list_t *list;       /* list to populate (required); a full list silently drops POIs */
    uint8_t type;               /* POI type to create (game-defined) */
    uint64_t salt;              /* decorrelates multiple detectors */

    /* --- Input: sampled field to test --- */
    uint8_t f_sampled;          /* sampled field to test (required) */

    /* --- Parameters; a zero/inverted value resolves to the default shown
           (resolved in place by the constructor) --- */
    float threshold;            /* create POI if the field value > threshold */
    float density;              /* probability per eligible tile, in [0,1]; 0 -> 0.001 */
    uint16_t size;              /* suggested footprint side in tiles (-> bbox); 0 -> 32 */
    int min_separation;         /* min tiles between POIs of the same type; 0 -> size,
                                   < 0 = off; checked against already-emitted POIs,
                                   in fill order */

    /* --- Optional: drain constraint (near river); NULL = off --- */
    const rlk_drain_t *drain;   /* pre-built drainage */
    float min_acc;              /* min flow accumulation, cells; 0 = any lattice cell */

    /* --- Optional: grid constraint (elevation/other); NULL = off --- */
    const rlk_grid_t *grid;     /* pre-built grid */
    float grid_min;             /* min grid value (inclusive); 0 -> unbounded below */
    float grid_max;             /* max grid value (inclusive); 0 -> unbounded above */
} rlk_wg_poi_cfg_t;

/* Build the POI detector stage. The cfg must outlive the wg. */
rlk_wg_stage_t rlk_wg_poi(rlk_wg_poi_cfg_t *cfg);

#endif /* RLK_WG_POI_H */
