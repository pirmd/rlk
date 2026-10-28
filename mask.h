#ifndef RLK_MASK_H
#define RLK_MASK_H

/* layer: 1, domain: base */

/*
 * Mono-band uint8 mask over a finite world rectangle: 0 = excluded,
 * 1..255 = weight (255 = full). Depending on usage: placement density
 * (worldgen), visibility (fov), passability, fog of war.
 *
 * Header-only. POD struct: initialize with designated initializers.
 *
 *   uint8_t buf[W * H];
 *   rlk_mask_t m = { .lattice = { .w = W, .h = H, .cellsize = 1,
 *                                 .origin_x = OX, .origin_y = OY }, .v = buf };
 *
 * Zero-allocation: caller provides the value buffer (w*h bytes).
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "err.h"
#include "geom.h"
#include "grid.h"
#include "tilemap.h"

typedef struct {
    rlk_lattice_t lattice;  /* cell geometry over the world */
    uint8_t *v;             /* w*h weights, row-major (v[j*w + i]); caller-owned */
} rlk_mask_t;

/* Weight at world position (x, y); 0 outside the mask. */
static inline int
rlk_mask_weight(const rlk_mask_t *m, int x, int y) {
    assert(m);
    int idx = rlk_lattice_index(&m->lattice, x, y);
    return idx < 0 ? 0 : m->v[(size_t)idx];
}

/* 1 if the position is allowed (weight > 0), 0 otherwise. */
static inline int
rlk_mask_allows(const rlk_mask_t *m, int x, int y) {
    return rlk_mask_weight(m, x, y) > 0;
}

/* Copy the geometry of a grid; buf (w*h bytes) provided by the caller. */
static inline void
rlk_mask_init_like(rlk_mask_t *m, const rlk_grid_t *g, uint8_t *buf) {
    assert(m && g && buf);
    m->lattice = g->lattice;
    m->v = buf;
}

/* Hard threshold: 255 if lo <= g[c] <= hi, else 0. Same geometry required. */
static inline rlk_err_t
rlk_mask_threshold(rlk_mask_t *m, const rlk_grid_t *g, float lo, float hi) {
    assert(m && g);
    if (!rlk_lattice_same(&m->lattice, &g->lattice))
        return RLK_E_INVALID;
    const int n = m->lattice.w * m->lattice.h;
    for (int k = 0; k < n; k++)
        m->v[k] = (uint8_t)((g->v[k] >= lo && g->v[k] <= hi) ? 255 : 0);
    return RLK_OK;
}

/* Ramp: weight 0 at v0, 255 at v1, linear in between (v1 < v0 allowed:
   inverted ramp. v0 == v1: hard threshold at v0). Same geometry required. */
static inline rlk_err_t
rlk_mask_ramp(rlk_mask_t *m, const rlk_grid_t *g, float v0, float v1) {
    assert(m && g);
    if (!rlk_lattice_same(&m->lattice, &g->lattice))
        return RLK_E_INVALID;
    const int n = m->lattice.w * m->lattice.h;
    for (int k = 0; k < n; k++) {
        float t;
        if (v1 == v0)
            t = g->v[k] >= v0 ? 1.0f : 0.0f;
        else
            t = (g->v[k] - v0) / (v1 - v0);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        m->v[k] = (uint8_t)(t * 255.0f + 0.5f);
    }
    return RLK_OK;
}

/* AND: dst[c] = min(dst[c], src[c]). RLK_E_INVALID if geometries differ. */
static inline rlk_err_t
rlk_mask_and(rlk_mask_t *dst, const rlk_mask_t *src) {
    assert(dst && src);
    if (!rlk_lattice_same(&dst->lattice, &src->lattice))
        return RLK_E_INVALID;
    const int n = dst->lattice.w * dst->lattice.h;
    for (int k = 0; k < n; k++)
        if (src->v[k] < dst->v[k])
            dst->v[k] = src->v[k];
    return RLK_OK;
}

/* Mask from a tilemap: 255 if props->flags[tile.type] has bit `flag`, else 0.
   A type outside the table has no flag. m->lattice must equal
   rlk_tilemap_lattice(tm) (cellsize 1). region: sub-rect to recompute
   (incremental update), NULL = all. */
static inline rlk_err_t
rlk_mask_from_tilemap(rlk_mask_t *m, const rlk_tilemap_t *tm,
                      const rlk_tile_props_t *props, uint8_t flag,
                      const rlk_rect_t *region) {
    assert(m && tm && props && props->flags);
    rlk_lattice_t tl = rlk_tilemap_lattice(tm);
    if (!rlk_lattice_same(&m->lattice, &tl))
        return RLK_E_INVALID;
    rlk_rect_t full = { tm->origin_x, tm->origin_y, tm->w, tm->h };
    rlk_rect_t r = region ? *region : full;
    rlk_rect_t is;
    if (!rlk_rect_intersect(&is, &r, &full))
        return RLK_OK;
    for (int y = is.y; y < is.y + is.h; y++) {
        for (int x = is.x; x < is.x + is.w; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(tm, x, y);
            uint8_t fl = (t->type < props->ntypes) ? props->flags[t->type] : 0;
            int idx = rlk_lattice_index(&m->lattice, x, y);
            m->v[(size_t)idx] = (uint8_t)((fl & flag) ? 255 : 0);
        }
    }
    return RLK_OK;
}

#endif /* RLK_MASK_H */
