#ifndef RL_VIEW_H
#define RL_VIEW_H

#include <assert.h>
#include <stdbool.h>

#include "tilemap.h"

/*
 * rlk view — a clamped camera/window onto a Tilemap.
 *
 * A view maps tilemap cells to a destination rectangle. It tracks a top-left
 * tilemap origin (ox, oy) in cell coordinates and a size (vw, vh) in cells.
 * The view is clamped so it never reads outside the tilemap; an oversized
 * view is shrunk to the tilemap.
 *
 * The view owns nothing and allocates nothing (compatible with the rlk
 * allocation convention, docs/ALLOCATION.md). It only computes which tilemap
 * cells map to which destination cells; rendering is performed by the user
 * (see demo/). This keeps rlk free of any rendering dependency.
 *
 * Inspired by the view module of PR #1, adapted to the buffer-provided
 * Tilemap (no hidden allocation).
 */
typedef struct {
    const rlk_tilemap_t *tm;   /* source tilemap (not owned)        */
    int ox, oy;                /* tilemap origin (top-left) in cells */
    int vw, vh;                /* view size in cells                */
} rlk_view_t;

/* Initialize a view onto tm with the given origin and size (in cells).
 * The origin/size are clamped to keep the view within tm; an oversized view
 * is shrunk to tm. Returns RLK_E_CONFIG on bad arguments. */
static inline rlk_err_t
rlk_view_init(rlk_view_t *v, const rlk_tilemap_t *tm,
              int ox, int oy, int vw, int vh) {
    assert(v);
    if (tm == NULL || vw <= 0 || vh <= 0) return RLK_E_CONFIG;
    /* shrink oversized view to the tilemap */
    if (vw > tm->width)  vw = tm->width;
    if (vh > tm->height) vh = tm->height;
    /* clamp origin so the view stays inside tm */
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox > tm->width  - vw) ox = tm->width  - vw;
    if (oy > tm->height - vh) oy = tm->height - vh;
    v->tm = tm;
    v->ox = ox; v->oy = oy;
    v->vw = vw; v->vh = vh;
    return RLK_OK;
}

/* Center the view so that tilemap cell (cx, cy) is at the view's center,
 * clamped to the tilemap bounds. */
static inline void
rlk_view_center(rlk_view_t *v, int cx, int cy) {
    assert(v && v->tm);
    int ox = cx - v->vw / 2;
    int oy = cy - v->vh / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox > v->tm->width  - v->vw) ox = v->tm->width  - v->vw;
    if (oy > v->tm->height - v->vh) oy = v->tm->height - v->vh;
    v->ox = ox; v->oy = oy;
}

/* Move the view origin by (dx, dy) cells, clamped to the tilemap bounds. */
static inline void
rlk_view_move(rlk_view_t *v, int dx, int dy) {
    assert(v && v->tm);
    int ox = v->ox + dx;
    int oy = v->oy + dy;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox > v->tm->width  - v->vw) ox = v->tm->width  - v->vw;
    if (oy > v->tm->height - v->vh) oy = v->tm->height - v->vh;
    v->ox = ox; v->oy = oy;
}

/* True if destination view cell (vx, vy) is within the view. */
static inline bool
rlk_view_in(const rlk_view_t *v, int vx, int vy) {
    assert(v);
    return vx >= 0 && vx < v->vw && vy >= 0 && vy < v->vh;
}

/* Map a destination view cell (vx, vy) to the tilemap cell it shows.
 * Returns true if the mapped cell is inside the tilemap (always true for a
 * clamped view); outputs tilemap coords via gx/gy regardless. */
static inline bool
rlk_view_map(const rlk_view_t *v, int vx, int vy, int *gx, int *gy) {
    assert(v && gx && gy);
    *gx = v->ox + vx;
    *gy = v->oy + vy;
    return rlk_tilemap_in_bounds(v->tm, *gx, *gy);
}

/* Get the tilemap cell shown at destination view cell (vx, vy).
 * Returns NULL for out-of-view cells. */
static inline rlk_tilecell_t *
rlk_view_get(const rlk_view_t *v, int vx, int vy) {
    assert(v);
    if (!rlk_view_in(v, vx, vy)) return NULL;
    int gx, gy;
    if (!rlk_view_map(v, vx, vy, &gx, &gy)) return NULL;
    return rlk_tilemap_get(v->tm, gx, gy);
}

/* Query solidity of the tilemap cell shown at view cell (vx, vy).
 * Out-of-view => solid (1). */
static inline int
rlk_view_is_solid(const rlk_view_t *v, int vx, int vy) {
    assert(v);
    if (!rlk_view_in(v, vx, vy)) return 1;
    int gx, gy;
    if (!rlk_view_map(v, vx, vy, &gx, &gy)) return 1;
    return rlk_tilemap_is_solid(v->tm, gx, gy);
}

#endif /* RL_VIEW_H */
