#ifndef RLK_PATH_H
#define RLK_PATH_H

/* layer: 2, domain: spatial */

/*
 * A* pathfinding with integer costs on a tile-aligned window
 * (rlk_lattice_t with cellsize == 1). 8-connected: a cardinal step costs
 * the target cell's enter cost, a diagonal step costs ceil(3/2 * cost)
 * and is only allowed when both adjacent cardinal cells are enterable
 * (no corner cutting). Deterministic: ties are broken by cell index,
 * never by memory addresses. Zero-allocation: the caller provides the
 * scratch buffers.
 *
 *   int g[W*H], parent[W*H], heap[W*H], heap_pos[W*H];
 *   uint8_t closed[W*H];
 *   rlk_path_scratch_t scr = { g, parent, heap, heap_pos, closed };
 *   int path[2 * W*H];   (x0,y0, x1,y1, ...) in world tile coords
 *   int n;
 *   rlk_err_t e = rlk_path_find(&cost, &scr, sx, sy, gx, gy, path, W*H, &n);
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "err.h"
#include "geom.h"
#include "tilemap.h"

#define RLK_PATH_BLOCKED   0        /* enter cost: the cell cannot be entered */

/* Cost source: a per-cell table OR a callback, exactly one must be set.
   Costs are 0..255: RLK_PATH_BLOCKED (0) = blocked, 1..255 = enter cost. */
typedef struct {
    rlk_lattice_t lattice;              /* window; cellsize must be 1 */
    const uint8_t *table;               /* enter cost per cell, row-major, or NULL */
    int (*fn)(void *ctx, int x, int y); /* cost callback (world tile coords), or NULL */
    void *ctx;                          /* callback context */
} rlk_path_cost_t;

/* Scratch buffers, caller-owned: four int arrays + one byte array, w*h each. */
typedef struct {
    int *g;          /* cost from the start cell */
    int *parent;     /* predecessor cell index, -1 = none */
    int *heap;       /* binary heap of cell indices (open list) */
    int *heap_pos;   /* heap position per cell, -1 = not in the heap */
    uint8_t *closed; /* settled flag per cell */
} rlk_path_scratch_t;

/* Find the cheapest path from (sx,sy) to (gx,gy), world tile coordinates.
   On RLK_OK, out holds *out_len points as (x0,y0, x1,y1, ...) pairs in world
   tile coordinates, start first, goal last (*out_len >= 1).
   RLK_E_NOTFOUND if the goal is unreachable or blocked.
   RLK_E_INVALID if a coordinate is outside the window, the window exceeds
   the built-in limit (1<<20 cells), no cost source is set, or cap < *out_len. */
rlk_err_t rlk_path_find(const rlk_path_cost_t *cost, rlk_path_scratch_t *scr,
                        int sx, int sy, int gx, int gy,
                        int *out, int cap, int *out_len);

/* Draw a path (as produced by rlk_path_find) onto a tilemap: each segment
   is brushed with rlk_tilemap_line (square brush of `width` tiles). */
void rlk_path_draw(rlk_tilemap_t *tm, const int *path, int n,
                   int width, rlk_tile_t tile);

#endif /* RLK_PATH_H */
