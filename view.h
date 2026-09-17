#ifndef RLK_VIEW_H
#define RLK_VIEW_H

#include <assert.h>
#include <stdbool.h>

#include "grid.h"

/*
 * View: a rectangular window onto a grid (a camera/fovea).
 *
 * A view maps grid cells to a destination rectangle on a target surface. It
 * tracks a top-left grid origin (ox, oy) in grid coordinates and a size
 * (vw, vh) in cells. The view is clamped so it never reads outside the grid.
 *
 * The view itself is graphics-backend agnostic: it only computes which grid
 * cells map to which destination cells. Rendering is performed by the user
 * (see demo for a PXL-based renderer). This keeps RLK free of any rendering
 * dependency.
 */
typedef struct {
    const rlk_grid_t *grid;  /* source grid (not owned)        */
    int ox, oy;              /* grid origin (top-left) in cells */
    int vw, vh;              /* view size in cells             */
} rlk_view_t;

/* Initialize a view onto grid with the given origin and size (in cells).
 * The origin/size are clamped to keep the view within the grid; an oversized
 * view is shrunk to the grid. Returns RLK_E_INVALID_PARAM on bad arguments. */
rlk_err_t rlk_view_init(rlk_view_t *v, const rlk_grid_t *grid,
                        int ox, int oy, int vw, int vh);

/* Center the view so that grid cell (cx, cy) is centered. The view is
 * clamped to the grid bounds so it never reads out of range. */
void rlk_view_center(rlk_view_t *v, int cx, int cy);

/* Move the view origin by (dx, dy) cells, clamped to the grid bounds. */
void rlk_view_move(rlk_view_t *v, int dx, int dy);

/* True if destination cell (vx, vy) within the view maps to a valid grid
 * cell (i.e. the view does not overhang the grid there). For views clamped to
 * the grid this is always true; it becomes relevant only for oversized views
 * that intentionally overhang, which rlk_view_init prevents. */
static inline bool
rlk_view_in(const rlk_view_t *v, int vx, int vy) {
    assert(v);
    return vx >= 0 && vx < v->vw && vy >= 0 && vy < v->vh;
}

/* Map a destination view cell (vx, vy) to the grid cell it shows.
 * Returns true if the mapped cell is inside the grid, false otherwise (in
 * which case the output grid coords are still set but may be out of range). */
static inline bool
rlk_view_map(const rlk_view_t *v, int vx, int vy, int *gx, int *gy) {
    assert(v);
    assert(gx && gy);
    *gx = v->ox + vx;
    *gy = v->oy + vy;
    return rlk_grid_in(v->grid, *gx, *gy);
}

/* Get the grid tile shown at destination view cell (vx, vy).
 * Returns RLK_TILE_VOID for out-of-range or overhanging cells. */
static inline rlk_tile_t
rlk_view_get(const rlk_view_t *v, int vx, int vy) {
    assert(v);
    if (!rlk_view_in(v, vx, vy))
        return RLK_TILE_VOID;
    int gx, gy;
    if (!rlk_view_map(v, vx, vy, &gx, &gy))
        return RLK_TILE_VOID;
    return rlk_grid_get(v->grid, gx, gy);
}

#endif /* RLK_VIEW_H */
