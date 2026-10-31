#ifndef RLK_SLOPE_H
#define RLK_SLOPE_H

/*
 * Slope of an elevation grid, Horn's 3x3 method. Header-only.
 *
 * Returns the slope at cell (i, j) as a rise/run ratio (the angle is
 * atan of it). zscale converts one grid unit of elevation into tiles of
 * height, so the derivatives are per tile: a [0,1] elevation field
 * spanning 128 tiles of relief uses zscale = 128.0f. It is a parameter,
 * not a grid field: a grid has no vertical dimension, only the caller
 * knows the world scale.
 *
 * Edge cells replicate their out-of-grid neighbours.
 * For the slope along the flow direction on a filled surface (D8 downslope),
 * see hydro.h -- the two notions are different by design.
 */

#include <assert.h>
#include <math.h>
#include <stddef.h>

#include "grid.h"

static inline float
rlk_slope_get(const rlk_grid_t *g, int i, int j, float zscale) {
    assert(g);
    const int w = g->lattice.w, h = g->lattice.h;
    assert(i >= 0 && i < w && j >= 0 && j < h);
    const float cs = (float)g->lattice.cellsize;

    const int im = i > 0 ? i - 1 : 0;
    const int ip = i < w - 1 ? i + 1 : w - 1;
    const int jm = j > 0 ? j - 1 : 0;
    const int jp = j < h - 1 ? j + 1 : h - 1;

    const float tl = g->values[(size_t)jm * (size_t)w + (size_t)im];
    const float tc = g->values[(size_t)jm * (size_t)w + (size_t)i];
    const float tr = g->values[(size_t)jm * (size_t)w + (size_t)ip];
    const float ml = g->values[(size_t)j * (size_t)w + (size_t)im];
    const float mr = g->values[(size_t)j * (size_t)w + (size_t)ip];
    const float bl = g->values[(size_t)jp * (size_t)w + (size_t)im];
    const float bc = g->values[(size_t)jp * (size_t)w + (size_t)i];
    const float br = g->values[(size_t)jp * (size_t)w + (size_t)ip];

    const float dzdx = zscale * ((tr + 2.0f * mr + br) - (tl + 2.0f * ml + bl))
                       / (8.0f * cs);
    const float dzdy = zscale * ((bl + 2.0f * bc + br) - (tl + 2.0f * tc + tr))
                       / (8.0f * cs);
    return sqrtf(dzdx * dzdx + dzdy * dzdy);
}

#endif /* RLK_SLOPE_H */
