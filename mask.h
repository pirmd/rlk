#ifndef RLK_MASK_H
#define RLK_MASK_H

/*
 * Mono-band uint8 mask over a finite world rect: 0 = excluded,
 * 1..255 = weight (255 = full). Placement density (worldgen), visibility
 * (fov), passability, fog of war. The caller provides the w*h weights
 * buffer (no allocation).
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "err.h"
#include "geom.h"
#include "tilemap.h"

typedef struct {
    rlk_lattice_t lattice;  /* cell geometry over the world */
    uint8_t *weights;       /* w*h weights, row-major (weights[j*w + i]); caller-owned */
} rlk_mask_t;

/* Weight of the cell containing world tile (x, y); 0 outside the mask.
   Integer counterpart of rlk_grid_get: no interpolation, and 0 (not
   clamped) outside — the right semantics for passability / fog. */
static inline int
rlk_mask_get(const rlk_mask_t *m, int x, int y) {
    assert(m);
    int idx = rlk_lattice_index(&m->lattice, x, y);
    return idx < 0 ? 0 : m->weights[(size_t)idx];
}

/* 1 if the position is allowed (weight > 0), 0 otherwise. */
static inline int
rlk_mask_allows(const rlk_mask_t *m, int x, int y) {
    return rlk_mask_get(m, x, y) > 0;
}

/* Mask from a tilemap: 255 if props->flags[tile.type] has bit `flag`, else 0.
   A type outside the table has no flag. m->lattice must equal
   rlk_tilemap_lattice(tm) (cellsize 1), else RLK_E_INVALID. dirty:
   sub-rect to recompute (incremental update), NULL = all. */
static inline rlk_err_t
rlk_mask_from_tilemap(rlk_mask_t *m, const rlk_tilemap_t *tm,
                      const rlk_tile_props_t *props, uint8_t flag,
                      const rlk_rect_t *dirty) {
    assert(m && tm && props && props->flags);
    rlk_lattice_t tl = rlk_tilemap_lattice(tm);
    if (!rlk_lattice_same(&m->lattice, &tl))
        return RLK_E_INVALID;
    rlk_rect_t full = { tm->origin_x, tm->origin_y, tm->w, tm->h };
    rlk_rect_t r = dirty ? *dirty : full;
    rlk_rect_t is;
    if (!rlk_rect_intersect(&is, &r, &full))
        return RLK_OK;
    for (int y = is.y; y < is.y + is.h; y++) {
        for (int x = is.x; x < is.x + is.w; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(tm, x, y);
            uint8_t fl = (t->type < props->ntypes) ? props->flags[t->type] : 0;
            int idx = rlk_lattice_index(&m->lattice, x, y);
            m->weights[(size_t)idx] = (uint8_t)((fl & flag) ? 255 : 0);
        }
    }
    return RLK_OK;
}

#endif /* RLK_MASK_H */
