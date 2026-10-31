#ifndef RLK_GRID_H
#define RLK_GRID_H

/*
 * Mono-band float raster over a finite world rect; the caller provides
 * the w*h values buffer (no allocation). Cell (i,j) holds the value at
 * its centre, world position (origin + (i+0.5)*cellsize); rlk_grid_get is
 * exact at cell centres, bilinear in between.
 */

#include <assert.h>
#include <stddef.h>

#include "fmath.h"
#include "geom.h"

typedef struct {
    rlk_lattice_t lattice;  /* cell geometry over the world */
    float *values;          /* w*h values, row-major (values[j*w + i]); caller-owned */
} rlk_grid_t;

/* World position of the centre of cell (i, j). */
static inline void
rlk_grid_center(const rlk_grid_t *g, int i, int j, float *px, float *py) {
    assert(g && px && py);
    *px = (float)g->lattice.origin_x + ((float)i + 0.5f) * (float)g->lattice.cellsize;
    *py = (float)g->lattice.origin_y + ((float)j + 0.5f) * (float)g->lattice.cellsize;
}

/* Cell containing world tile (wx, wy). Returns 1 and sets *i, *j if inside,
   0 otherwise (outputs untouched). Exact for negative coordinates. */
static inline int
rlk_grid_cell_of(const rlk_grid_t *g, int wx, int wy, int *i, int *j) {
    assert(g && i && j);
    int idx = rlk_lattice_index(&g->lattice, wx, wy);
    if (idx < 0) return 0;
    *i = idx % g->lattice.w;
    *j = idx / g->lattice.w;
    return 1;
}

/* Bilinear value at a world position. Outside the grid: clamped to edges. */
static inline float
rlk_grid_get(const rlk_grid_t *g, float px, float py) {
    assert(g);
    float gx = (px - (float)g->lattice.origin_x) / (float)g->lattice.cellsize - 0.5f;
    float gy = (py - (float)g->lattice.origin_y) / (float)g->lattice.cellsize - 0.5f;
    gx = rlk_clampf(gx, 0.0f, (float)(g->lattice.w - 1));
    gy = rlk_clampf(gy, 0.0f, (float)(g->lattice.h - 1));
    int i0 = (int)gx, j0 = (int)gy;
    int i1 = i0 + 1 < g->lattice.w ? i0 + 1 : g->lattice.w - 1;
    int j1 = j0 + 1 < g->lattice.h ? j0 + 1 : g->lattice.h - 1;
    float fx = gx - (float)i0, fy = gy - (float)j0;
    float a = g->values[(size_t)j0 * (size_t)g->lattice.w + (size_t)i0];
    float b = g->values[(size_t)j0 * (size_t)g->lattice.w + (size_t)i1];
    float c = g->values[(size_t)j1 * (size_t)g->lattice.w + (size_t)i0];
    float d = g->values[(size_t)j1 * (size_t)g->lattice.w + (size_t)i1];
    float top = a + (b - a) * fx, bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}

/* Smallest and largest cell value. */
static inline void
rlk_grid_minmax(const rlk_grid_t *g, float *lo, float *hi) {
    assert(g && lo && hi);
    float a = g->values[0], b = g->values[0];
    for (int k = 1; k < g->lattice.w * g->lattice.h; k++) {
        if (g->values[k] < a) a = g->values[k];
        if (g->values[k] > b) b = g->values[k];
    }
    *lo = a;
    *hi = b;
}

#endif /* RLK_GRID_H */
