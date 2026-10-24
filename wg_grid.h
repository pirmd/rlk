#ifndef RLK_WG_GRID_H
#define RLK_WG_GRID_H

/*
 * Coarse float grid over a finite world rectangle, with bilinear sampling.
 *
 * Zero-allocation: caller provides the value buffer (w*h floats).
 * Deterministic: fill from pure functions of the seed, rebuild from the seed.
 * Finite: covers [ox, ox+w*cell) x [oy, oy+h*cell) world tiles. Outside,
 * sampling clamps to the nearest edge cell.
 *
 * Cell (i,j) holds the value at its centre, world position
 * (ox + (i+0.5)*cell, oy + (j+0.5)*cell). Sampling at a centre returns
 * the exact cell value; in between it interpolates bilinearly.
 *
 * Memory: w*h*4 bytes. 256x256 cells of 8 tiles = 2048x2048-tile world for 256 KB.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "fmath.h"

typedef struct {
    int w, h;      /* cells, >= 1 */
    int cell;      /* tiles per cell side, >= 1 */
    int ox, oy;    /* world tile of the grid's top-left corner */
    float *v;      /* w*h values, row-major (v[j*w + i]); caller-owned */
} rlk_wg_grid_t;

/* A value computed at a world position. Must be a pure function. */
typedef float (*rlk_wg_grid_fn)(float px, float py, const void *user);

/* --- PUBLIC API ---------------------------------------------------------- */

/* Bind a grid to caller storage (w*h floats). Values are zeroed. */
void rlk_wg_grid_init(rlk_wg_grid_t *g, int w, int h, int cell, int ox, int oy,
                      float *buf);

/* Fill every cell with fn(centre, user). Cells are visited row by row. */
void rlk_wg_grid_fill(rlk_wg_grid_t *g, rlk_wg_grid_fn fn, const void *user);

/* --- HELPERS (static inline) --------------------------------------------- */

/* World position of the centre of cell (i, j). */
static inline void
rlk_wg_grid_center(const rlk_wg_grid_t *g, int i, int j, float *px, float *py) {
    assert(g && px && py);
    *px = (float)g->ox + ((float)i + 0.5f) * (float)g->cell;
    *py = (float)g->oy + ((float)j + 0.5f) * (float)g->cell;
}

/* Cell containing world tile (wx, wy). Returns 1 and sets *i, *j if inside,
   0 otherwise (outputs untouched). Exact for negative coordinates. */
static inline int
rlk_wg_grid_cell_of(const rlk_wg_grid_t *g, int wx, int wy, int *i, int *j) {
    assert(g && i && j);
    int dx = wx - g->ox, dy = wy - g->oy;
    int qx = dx / g->cell, qy = dy / g->cell;
    if (dx % g->cell < 0) qx--;  /* floor division */
    if (dy % g->cell < 0) qy--;
    if (qx < 0 || qx >= g->w || qy < 0 || qy >= g->h) return 0;
    *i = qx;
    *j = qy;
    return 1;
}

/* Bilinear sample at a world position. Outside the grid: clamped to edges. */
static inline float
rlk_wg_grid_sample(const rlk_wg_grid_t *g, float px, float py) {
    assert(g);
    float gx = (px - (float)g->ox) / (float)g->cell - 0.5f;
    float gy = (py - (float)g->oy) / (float)g->cell - 0.5f;
    gx = rlk_clampf(gx, 0.0f, (float)(g->w - 1));
    gy = rlk_clampf(gy, 0.0f, (float)(g->h - 1));
    int i0 = (int)gx, j0 = (int)gy;
    int i1 = i0 + 1 < g->w ? i0 + 1 : g->w - 1;
    int j1 = j0 + 1 < g->h ? j0 + 1 : g->h - 1;
    float fx = gx - (float)i0, fy = gy - (float)j0;
    float a = g->v[(size_t)j0 * (size_t)g->w + (size_t)i0];
    float b = g->v[(size_t)j0 * (size_t)g->w + (size_t)i1];
    float c = g->v[(size_t)j1 * (size_t)g->w + (size_t)i0];
    float d = g->v[(size_t)j1 * (size_t)g->w + (size_t)i1];
    float top = a + (b - a) * fx, bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}

/* Smallest and largest cell value. */
static inline void
rlk_wg_grid_minmax(const rlk_wg_grid_t *g, float *lo, float *hi) {
    assert(g && lo && hi);
    float a = g->v[0], b = g->v[0];
    for (int k = 1; k < g->w * g->h; k++) {
        if (g->v[k] < a) a = g->v[k];
        if (g->v[k] > b) b = g->v[k];
    }
    *lo = a;
    *hi = b;
}

#endif /* RLK_WG_GRID_H */
