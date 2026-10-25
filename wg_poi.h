#ifndef RLK_WG_POI_H
#define RLK_WG_POI_H

/*
 * Points of Interest: discrete markers on the world map that trigger
 * local content generation (caves, dungeons, villages, towers).
 *
 * A POI is pure data: where (wx, wy), what (kind), how (seed, size),
 * and context (flags like NEAR_RIVER, ON_HILL).
 *
 * The poi_list is a fixed-capacity, caller-owned array. No allocation.
 * Typical usage: fill a list during wg_nf_fill (via wg_nf_poi stage),
 * then iterate to generate local content.
 */

#include <assert.h>
#include <stdint.h>

#include "err.h"
#include "hash.h"

/* --- CONSTANTS ----------------------------------------------------------- */
#define RLK_WG_POI_MAX        256   /* max POI per list (per chunk/zone) */
#define RLK_WG_POI_DATA_SIZE  8     /* bytes of type-specific data */

/* POI kinds: what to generate at this location. */
typedef enum {
    RLK_WG_POI_CAVE    = 0,   /* -> wg_ca */
    RLK_WG_POI_DUNGEON = 1,   /* -> wg_bsp */
    RLK_WG_POI_VILLAGE = 2,   /* -> future structure module */
    RLK_WG_POI_TOWER   = 3,   /* -> future */
    RLK_WG_POI_RUIN    = 4,   /* -> future */
    RLK_WG_POI_CUSTOM  = 255, /* user-defined */
} rlk_wg_poi_kind_t;

/* Context flags: how the POI was detected (set by wg_nf_poi stage). */
#define RLK_WG_POI_NEAR_RIVER  0x01  /* near a river (flow_acc > threshold) */
#define RLK_WG_POI_ON_HILL     0x02  /* on elevated terrain */
#define RLK_WG_POI_IN_FOREST   0x04  /* in a forest biome */
#define RLK_WG_POI_COASTAL     0x08  /* near sea (elevation ~ sea_level) */

/* --- TYPES --------------------------------------------------------------- */

/* A single Point of Interest. POD: fill fields directly. */
typedef struct {
    int wx, wy;                         /* world position (center) */
    rlk_wg_poi_kind_t kind;             /* what to generate */
    uint64_t seed;                      /* deterministic seed for the generator */
    uint16_t size;                      /* suggested size in tiles (e.g. 48) */
    uint8_t flags;                      /* RLK_WG_POI_* context flags */
    uint8_t data[RLK_WG_POI_DATA_SIZE]; /* type-specific extra data */
} rlk_wg_poi_t;

/* Fixed-capacity list of POI. Caller-owned, no allocation. */
typedef struct {
    rlk_wg_poi_t items[RLK_WG_POI_MAX];
    int count;
} rlk_wg_poi_list_t;

/* --- PUBLIC API ---------------------------------------------------------- */

/* Zero the list (count = 0). Call before reusing a list. */
void rlk_wg_poi_list_init(rlk_wg_poi_list_t *list);

/* Append a POI. Returns RLK_E_INVALID if list is full or poi is NULL.
 * Does NOT check for duplicates (caller's responsibility). */
rlk_err_t rlk_wg_poi_add(rlk_wg_poi_list_t *list, const rlk_wg_poi_t *poi);

/* Find the nearest POI of a given kind within `radius` tiles of (wx, wy).
 * Returns NULL if none found. */
const rlk_wg_poi_t *rlk_wg_poi_find(const rlk_wg_poi_list_t *list,
                                    int wx, int wy, int radius,
                                    rlk_wg_poi_kind_t kind);

/* Count POIs of a given kind in the list. */
int rlk_wg_poi_count(const rlk_wg_poi_list_t *list, rlk_wg_poi_kind_t kind);

/* --- HELPERS (static inline) --------------------------------------------- */

/* Deterministic seed for a POI at (wx, wy) from world seed + kind + salt. */
static inline uint64_t
rlk_wg_poi_seed(uint64_t world_seed, int wx, int wy,
                rlk_wg_poi_kind_t kind, uint64_t salt) {
    uint64_t h = rlk_hash_combine2(world_seed, wx, wy);
    h = rlk_hash_combine2(h, (int)kind, 0);
    return rlk_hash_sub(h, salt);
}

/* Check if a POI has a given context flag. */
static inline int
rlk_wg_poi_has_flag(const rlk_wg_poi_t *poi, uint8_t flag) {
    assert(poi);
    return (poi->flags & flag) != 0;
}

#endif /* RLK_WG_POI_H */
