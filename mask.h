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
#include <string.h>

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

/* Flood fill 4-connected from world tile (x,y) over cells with weight > 0.
   labels (w*h ints, caller-owned) must be zeroed on entry; reached cells
   get `label` (must be > 0). stack: caller-owned int buffer of w*h cells.
   Returns the number of labelled cells, 0 if (x,y) is outside or excluded.
 *
 *   int labels[W*H], stack[W*H];
 *   memset(labels, 0, sizeof(labels));
 *   int n = rlk_mask_flood(&m, x, y, 1, labels, stack);
 */
static inline int
rlk_mask_flood(const rlk_mask_t *m, int x, int y, int label,
               int *labels, int *stack) {
    assert(m && labels && stack);
    assert(label > 0);
    int idx = rlk_lattice_index(&m->lattice, x, y);
    if (idx < 0 || m->weights[(size_t)idx] == 0)
        return 0;
    static const int d4[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    const int w = m->lattice.w;
    int top = 0, count = 0;
    stack[top++] = idx;
    labels[(size_t)idx] = label;
    while (top > 0) {
        int c = stack[--top];
        count++;
        int cx = c % w, cy = c / w;
        for (int k = 0; k < 4; k++) {
            int nx = cx + d4[k][0], ny = cy + d4[k][1];
            if (nx < 0 || nx >= w || ny < 0 || ny >= m->lattice.h)
                continue;
            int ni = ny * w + nx;
            if (labels[(size_t)ni] == label || m->weights[(size_t)ni] == 0)
                continue;
            labels[(size_t)ni] = label;
            stack[top++] = ni;
        }
    }
    return count;
}

/* Label every 4-connected component of weight > 0 cells: labels (w*h ints,
   caller-owned) is zeroed, then each component receives 1..ncomp in scan
   order (0 = excluded cell). stack: caller-owned int buffer of w*h cells.
   Returns the number of components.
 *
 *   int labels[W*H], stack[W*H];
 *   int ncomp = rlk_mask_components(&m, labels, stack);
 */
static inline int
rlk_mask_components(const rlk_mask_t *m, int *labels, int *stack) {
    assert(m && labels && stack);
    const int w = m->lattice.w, h = m->lattice.h;
    const int n = w * h;
    memset(labels, 0, (size_t)n * sizeof(int));
    int ncomp = 0;
    for (int idx = 0; idx < n; idx++) {
        if (m->weights[(size_t)idx] == 0 || labels[(size_t)idx] != 0)
            continue;
        int x = m->lattice.origin_x + (idx % w) * m->lattice.cellsize;
        int y = m->lattice.origin_y + (idx / w) * m->lattice.cellsize;
        ncomp++;
        rlk_mask_flood(m, x, y, ncomp, labels, stack);
    }
    return ncomp;
}

#endif /* RLK_MASK_H */
