#ifndef RLK_POI_H
#define RLK_POI_H

/*
 * Points of Interest: discrete markers on the world map that trigger local
 * content generation (caves, dungeons, villages, towers). Pure data:
 * where (x, y + bbox), what (type), how (seed), caller-owned context
 * (gen, level). type/gen/level are game-defined; rlk writes 0 at creation
 * and never reads them (type has the same role and width as
 * rlk_tile_t.type).
 *
 * The list is a caller-owned fixed-capacity array (no allocation); the
 * index in a persistent list is the stable id of a POI (never reordered
 * nor compacted).
 *
 * Overlap rule: a generator writes only inside the bbox of its POI; on
 * overlap the first POI in emission order wins (see rlk_poi_at).
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "err.h"
#include "geom.h"
#include "hash.h"

typedef struct {
    int        x, y;      /* world anchor (e.g. entrance), in tiles */
    rlk_rect_t bbox;      /* world footprint, in tiles (contains (x, y)) */
    uint8_t    type;      /* game-defined type (same role/width as rlk_tile_t.type) */
    uint64_t   seed;      /* per-POI seed */
    uint8_t    gen;       /* game-defined; rlk writes 0 (placed by wg) */
    uint8_t    level;     /* game-defined target map (0 = overworld) */
} rlk_poi_t;

/* Caller-owned, fixed-capacity list. */
typedef struct {
    rlk_poi_t *pois;
    int n, cap;
} rlk_poi_list_t;

/* Append a POI. RLK_E_INVALID if the list is full.
   Does NOT check for duplicates (caller's responsibility). */
static inline rlk_err_t
rlk_poi_push(rlk_poi_list_t *l, const rlk_poi_t *p) {
    assert(l && p);
    if (l->n >= l->cap)
        return RLK_E_INVALID;
    l->pois[(size_t)l->n] = *p;
    l->n++;
    return RLK_OK;
}

/* First POI (emission order) whose bbox contains (x, y), else NULL.
   Encodes the overlap rule: on overlapping bboxes, the first POI wins. */
static inline const rlk_poi_t *
rlk_poi_at(const rlk_poi_list_t *l, int x, int y) {
    assert(l);
    for (int i = 0; i < l->n; i++)
        if (rlk_rect_contains(&l->pois[(size_t)i].bbox, x, y))
            return &l->pois[(size_t)i];
    return NULL;
}

/* Nearest POI of `type` within `radius` tiles of (x, y). NULL if none. */
static inline const rlk_poi_t *
rlk_poi_find(const rlk_poi_list_t *l, int x, int y, int radius, uint8_t type) {
    assert(l);
    assert(radius >= 0);
    const rlk_poi_t *best = NULL;
    int best_d2 = radius * radius + 1;
    for (int i = 0; i < l->n; i++) {
        const rlk_poi_t *p = &l->pois[(size_t)i];
        if (p->type != type)
            continue;
        int dx = p->x - x;
        int dy = p->y - y;
        int d2 = dx * dx + dy * dy;
        if (d2 <= radius * radius && d2 < best_d2) {
            best = p;
            best_d2 = d2;
        }
    }
    return best;
}

/* Deterministic seed for a POI at (x, y) from world seed + type + salt. */
static inline uint64_t
rlk_poi_seed(uint64_t world_seed, int x, int y, uint8_t type, uint64_t salt) {
    uint64_t h = rlk_hash_combine2(world_seed, x, y);
    h = rlk_hash_combine2(h, (int)type, 0);
    return rlk_hash_sub(h, salt);
}

#endif /* RLK_POI_H */
