#ifndef RLK_WG_NF_POI_H
#define RLK_WG_NF_POI_H

/*
 * Optional wg_nf stage: detect Points of Interest and populate a rlk_wg_poi_list_t.
 *
 * For each tile, evaluates conditions (field threshold, optional flow/grid
 * constraints, density) and creates a POI when all conditions are met.
 *
 * Usage:
 *   rlk_wg_poi_list_t pois;
 *   rlk_wg_nf_poi_t poi_cfg = {
 *       .list = &pois,
 *       .kind = RLK_WG_POI_CAVE,
 *       .field = 1,            // humidity field slot
 *       .threshold = 0.6f,
 *       .density = 0.002f,
 *       .size = 48,
 *       .flow = &flow_data,    // optional: near-river constraint
 *       .min_acc = 300.0f,
 *       .salt = 0xCAVE,
 *   };
 *   rlk_wg_nf_poi_add(&nf, &poi_cfg);
 *   rlk_wg_poi_list_init(&pois);
 *   rlk_wg_nf_fill(&nf, ox, oy, tm);  // fills tm AND pois
 */

#include "wg_nf.h"
#include "wg_poi.h"
#include "wg_flow.h"
#include "wg_grid.h"

/* POI detector stage configuration. */
typedef struct {
    /* --- Target --- */
    rlk_wg_poi_list_t *list;        /* list to populate; must outlive the wg */
    rlk_wg_poi_kind_t kind;         /* POI kind to create */
    uint64_t salt;                  /* decorrelates multiple detectors */

    /* --- Field condition (required) --- */
    uint8_t field;                  /* field slot to test (0..nsampled+nderived-1) */
    float threshold;                /* create POI if field[field] > threshold */
    float density;                  /* probability per eligible tile (0..1) */
    uint16_t size;                  /* suggested content size in tiles */

    /* --- Optional: flow constraint (near river) --- */
    const rlk_wg_flow_t *flow;      /* NULL = no constraint */
    const rlk_wg_grid_t *flow_grid; /* elevation grid matching flow; required if flow set */
    float min_acc;                  /* min flow accumulation in cells */

    /* --- Optional: grid constraint (elevation/other) --- */
    const rlk_wg_grid_t *grid;      /* NULL = no constraint */
    float grid_min;                 /* min grid value (inclusive) */
    float grid_max;                 /* max grid value (inclusive) */

    /* --- Deduplication --- */
    int min_separation;             /* min tiles between POIs of same kind (0=off) */
} rlk_wg_nf_poi_t;

/* Stage function (use directly with rlk_wg_nf_add_stage if managing stages by hand). */
void rlk_wg_nf_poi_stage(rlk_wg_nf_ctx_t *ctx, const void *data);

/* Resolve zero defaults: density 0->0.001, size 0->32, min_separation 0->size.
 * Call once after filling. */
void rlk_wg_nf_poi_init(rlk_wg_nf_poi_t *cfg);

/* Init cfg, then register it as a stage of wg. cfg must outlive the wg. */
rlk_err_t rlk_wg_nf_poi_add(rlk_wg_nf_t *wg, rlk_wg_nf_poi_t *cfg);

#endif /* RLK_WG_NF_POI_H */
