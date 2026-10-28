#ifndef RLK_SLOPE_H
#define RLK_SLOPE_H

/* layer: 2, domain: terrain */

/*
 * Slope of an elevation grid, Horn's 3x3 method. Header-only.
 *
 * The slope is a ratio (rise / run) at cell (i, j). zscale converts one
 * elevation unit into tiles of height: a normalized [0,1] elevation field
 * representing 128 tiles of relief uses zscale = 128.0f — same role as
 * gdaldem's -s option. zscale is a function parameter, not a grid field:
 * grids without a vertical dimension have no use for it.
 *
 * Edge cells replicate their out-of-grid neighbours.
 */

#include <assert.h>
#include <math.h>
#include <stddef.h>

#include "grid.h"

static inline float
rlk_slope_at(const rlk_grid_t *g, int i, int j, float zscale) {
    assert(g);
    const int w = g->lattice.w, h = g->lattice.h;
    assert(i >= 0 && i < w && j >= 0 && j < h);
    const float cs = (float)g->lattice.cellsize;

    const int im = i > 0 ? i - 1 : 0;
    const int ip = i < w - 1 ? i + 1 : w - 1;
    const int jm = j > 0 ? j - 1 : 0;
    const int jp = j < h - 1 ? j + 1 : h - 1;

    const float tl = g->v[(size_t)jm * (size_t)w + (size_t)im];
    const float tc = g->v[(size_t)jm * (size_t)w + (size_t)i];
    const float tr = g->v[(size_t)jm * (size_t)w + (size_t)ip];
    const float ml = g->v[(size_t)j * (size_t)w + (size_t)im];
    const float mr = g->v[(size_t)j * (size_t)w + (size_t)ip];
    const float bl = g->v[(size_t)jp * (size_t)w + (size_t)im];
    const float bc = g->v[(size_t)jp * (size_t)w + (size_t)i];
    const float br = g->v[(size_t)jp * (size_t)w + (size_t)ip];

    const float dzdx = zscale * ((tr + 2.0f * mr + br) - (tl + 2.0f * ml + bl))
                       / (8.0f * cs);
    const float dzdy = zscale * ((bl + 2.0f * bc + br) - (tl + 2.0f * tc + tr))
                       / (8.0f * cs);
    return sqrtf(dzdx * dzdx + dzdy * dzdy);
}

#endif /* RLK_SLOPE_H */
