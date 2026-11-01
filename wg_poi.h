#ifndef RLK_WG_POI_H
#define RLK_WG_POI_H

/*
 * Optional wg stage: POI detector — fills a rlk_poi_list_t during the
 * fill pass; the POI list is the work queue for the second (local)
 * generation level. POI anchor = the tile, bbox = `size` x `size` around
 * it, type game-defined (the library defines none).
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
 *   rlk_wg_fill(&wg, 0, 0, &tm);  // fills tm AND pois
 */

#include "wg.h"
#include "poi.h"
#include "drain.h"
#include "grid.h"

/* POI detector stage configuration; treat as const after construction. */
typedef struct {
    /* --- Target --- */
    rlk_poi_list_t *list;           /* list to populate */
    uint8_t type;                   /* POI type to create (game-defined) */
    uint64_t salt;                  /* decorrelates multiple detectors */

    /* --- Field condition (required): f_sampled if set, else slot_derived --- */
    uint8_t f_sampled;              /* sampled field to test, or RLK_WG_NONE */
    uint8_t slot_derived;           /* derived slot to test, or RLK_WG_NONE */
    float threshold;                /* create POI if the field value > threshold */
    float density;                  /* probability per eligible tile (0..1) */
    uint16_t size;                  /* suggested footprint side in tiles (-> bbox) */

    /* --- Optional: drain constraint (near river) --- */
    const rlk_drain_t *drain;       /* NULL = no constraint */
    float min_acc;                  /* min flow accumulation in cells */

    /* --- Optional: grid constraint (elevation/other) --- */
    const rlk_grid_t *grid;      /* NULL = no constraint */
    float grid_min;                 /* min grid value (inclusive) */
    float grid_max;                 /* max grid value (inclusive) */

    /* --- Deduplication --- */
    int min_separation;             /* min tiles between POIs of same type (0->size) */
} rlk_wg_poi_cfg_t;

/* Build the POI detector stage; zero defaults (density 0->0.001, size
   0->32, min_separation 0->size) are resolved in place. The cfg must
   outlive the wg. */
rlk_wg_stage_t rlk_wg_poi(rlk_wg_poi_cfg_t *cfg);

#endif /* RLK_WG_POI_H */
