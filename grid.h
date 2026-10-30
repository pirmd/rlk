#ifndef RLK_GRID_H
#define RLK_GRID_H

/*
 * Mono-band float raster over a finite world rectangle, with bilinear sampling.
 *
 * Header-only. POD struct: initialize with designated initializers.
 *
 *   float buf[W * H];
 *   rlk_grid_t g = { .lattice = { .w = W, .h = H, .cellsize = CELL,
 *                                  .origin_x = OX, .origin_y = OY }, .v = buf };
 *   for (int j = 0; j < H; j++)
 *       for (int i = 0; i < W; i++)
 *           g.v[j*W + i] = my_fn(...);
 *
 * Cell (i,j) holds the value at its centre, world position
 * (origin_x + (i+0.5)*cellsize, origin_y + (j+0.5)*cellsize). Sampling at a centre returns
 * the exact cell value; in between it interpolates bilinearly.
 *
 * Zero-allocation: caller provides the value buffer (w*h floats).
 * Deterministic: fill from pure functions of the seed, rebuild from the seed.
 * Finite: covers [origin_x, origin_x+w*cellsize) x [origin_y, origin_y+h*cellsize) world tiles. Outside,
 * sampling clamps to the nearest edge cell.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "fmath.h"
#include "geom.h"

typedef struct {
    rlk_lattice_t lattice;  /* cell geometry over the world */
    float *v;              /* w*h values, row-major (v[j*w + i]); caller-owned */
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

/* Bilinear sample at a world position. Outside the grid: clamped to edges. */
static inline float
rlk_grid_sample(const rlk_grid_t *g, float px, float py) {
    assert(g);
    float gx = (px - (float)g->lattice.origin_x) / (float)g->lattice.cellsize - 0.5f;
    float gy = (py - (float)g->lattice.origin_y) / (float)g->lattice.cellsize - 0.5f;
    gx = rlk_clampf(gx, 0.0f, (float)(g->lattice.w - 1));
    gy = rlk_clampf(gy, 0.0f, (float)(g->lattice.h - 1));
    int i0 = (int)gx, j0 = (int)gy;
    int i1 = i0 + 1 < g->lattice.w ? i0 + 1 : g->lattice.w - 1;
    int j1 = j0 + 1 < g->lattice.h ? j0 + 1 : g->lattice.h - 1;
    float fx = gx - (float)i0, fy = gy - (float)j0;
    float a = g->v[(size_t)j0 * (size_t)g->lattice.w + (size_t)i0];
    float b = g->v[(size_t)j0 * (size_t)g->lattice.w + (size_t)i1];
    float c = g->v[(size_t)j1 * (size_t)g->lattice.w + (size_t)i0];
    float d = g->v[(size_t)j1 * (size_t)g->lattice.w + (size_t)i1];
    float top = a + (b - a) * fx, bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}

/* Smallest and largest cell value. */
static inline void
rlk_grid_minmax(const rlk_grid_t *g, float *lo, float *hi) {
    assert(g && lo && hi);
    float a = g->v[0], b = g->v[0];
    for (int k = 1; k < g->lattice.w * g->lattice.h; k++) {
        if (g->v[k] < a) a = g->v[k];
        if (g->v[k] > b) b = g->v[k];
    }
    *lo = a;
    *hi = b;
}

#endif /* RLK_GRID_H */
