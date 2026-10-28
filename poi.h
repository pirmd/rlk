#ifndef RLK_POI_H
#define RLK_POI_H

/* layer: 1, domain: base */

/*
 * Points of Interest: discrete markers on the world map that trigger local
 * content generation (caves, dungeons, villages, towers).
 *
 * A POI is pure data: where (x, y + bbox), what (type), how (seed), and
 * generation context (gen, level). type is an opaque game-defined value,
 * same role as rlk_tile_t.type: the library defines no POI types.
 *
 * The list is a caller-owned array. No allocation. Init is POD:
 *
 *   rlk_poi_t buf[256];
 *   rlk_poi_list_t q = { buf, 0, 256 };
 *
 * Identity: the index in a persistent rlk_poi_list_t is the stable id of a
 * POI. Such a list is never reordered nor compacted.
 *
 * Overlap rule: a generator writes only inside the bbox of its POI. If two
 * bboxes overlap (rlk_rect_overlaps), the first POI processed wins and the
 * second is ignored; processing order is deterministic (emission order).
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
    uint32_t   type;      /* game-defined type (same role as rlk_tile_t.type) */
    uint64_t   seed;      /* per-POI seed */
    uint8_t    gen;       /* generation only: 0 = placed by wg_nf, +1 per emitted POI */
    uint8_t    level;     /* target level/map (0 = overworld) */
} rlk_poi_t;

/* Caller-owned, fixed-capacity list. */
typedef struct {
    rlk_poi_t *v;
    int n, cap;
} rlk_poi_list_t;

/* Append a POI. RLK_E_INVALID if the list is full.
   Does NOT check for duplicates (caller's responsibility). */
static inline rlk_err_t
rlk_poi_push(rlk_poi_list_t *l, const rlk_poi_t *p) {
    assert(l && p);
    if (l->n >= l->cap)
        return RLK_E_INVALID;
    l->v[(size_t)l->n] = *p;
    l->n++;
    return RLK_OK;
}

/* Nearest POI of `type` within `radius` tiles of (x, y). NULL if none. */
static inline const rlk_poi_t *
rlk_poi_find(const rlk_poi_list_t *l, int x, int y, int radius, uint32_t type) {
    assert(l);
    assert(radius >= 0);
    const rlk_poi_t *best = NULL;
    int best_d2 = radius * radius + 1;
    for (int i = 0; i < l->n; i++) {
        const rlk_poi_t *p = &l->v[(size_t)i];
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

/* Count POIs of `type` in the list. */
static inline int
rlk_poi_count(const rlk_poi_list_t *l, uint32_t type) {
    assert(l);
    int n = 0;
    for (int i = 0; i < l->n; i++)
        if (l->v[(size_t)i].type == type)
            n++;
    return n;
}

/* Deterministic seed for a POI at (x, y) from world seed + type + salt. */
static inline uint64_t
rlk_poi_seed(uint64_t world_seed, int x, int y, uint32_t type, uint64_t salt) {
    uint64_t h = rlk_hash_combine2(world_seed, x, y);
    h = rlk_hash_combine2(h, (int)type, 0);
    return rlk_hash_sub(h, salt);
}

#endif /* RLK_POI_H */
