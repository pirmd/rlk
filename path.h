#ifndef RLK_PATH_H
#define RLK_PATH_H

/*
 * A* pathfinding with integer costs on a tile-aligned window
 * (rlk_lattice_t with cellsize == 1). 8-connected: a cardinal step costs
 * 2 * enter cost, a diagonal step costs 3 * enter cost (exact 1.5 ratio,
 * no rounding) and is only allowed when both adjacent cardinal cells are
 * enterable (no corner cutting). The heuristic is the octile distance
 * scaled the same way, so it is admissible and consistent. Deterministic:
 * heap ties are broken by larger g, then by cell index, never by memory
 * addresses. Zero-allocation: the caller provides the scratch buffers.
 *
 * rlk_path_find resets the whole window (O(w*h)) on every call: pass
 * small windows around the entity, not whole maps.
 *
 *   int g[W*H], parent[W*H], heap[W*H], heap_pos[W*H], f[W*H];
 *   uint8_t closed[W*H];
 *   rlk_path_scratch_t scr = { g, parent, heap, heap_pos, f, closed };
 *   int path[2 * W*H];   (x0,y0, x1,y1, ...) in world tile coords
 *   int n;
 *   rlk_err_t e = rlk_path_find(&cost, &scr, sx, sy, gx, gy, path, W*H, &n);
 */

#include <stdint.h>

#include "err.h"
#include "geom.h"
#include "tilemap.h"

#define RLK_PATH_BLOCKED   0        /* enter cost: the cell cannot be entered */

/* Cost source: a per-cell table OR a callback, exactly one must be set.
   Costs are 0..255: RLK_PATH_BLOCKED (0) = blocked, 1..255 = enter cost.
   Callback values outside 0..255 are clamped into it. */
typedef struct {
    rlk_lattice_t lattice;              /* window; cellsize must be 1 */
    const uint8_t *table;               /* enter cost per cell, row-major, or NULL */
    int (*fn)(void *ctx, int x, int y); /* cost callback (world tile coords), or NULL */
    void *ctx;                          /* callback context */
} rlk_path_cost_t;

/* Scratch buffers, caller-owned: five int arrays + one byte array, w*h each. */
typedef struct {
    int *g;          /* cost from the start cell */
    int *parent;     /* predecessor cell index, -1 = none */
    int *heap;       /* binary heap of cell indices (open list) */
    int *heap_pos;   /* heap position per cell, -1 = not in the heap */
    int *f;          /* f = g + octile distance to the goal (heap ordering) */
    uint8_t *closed; /* settled flag per cell */
} rlk_path_scratch_t;

/* Find the cheapest path from (sx,sy) to (gx,gy), world tile coordinates.
   On RLK_OK, out holds *out_len points as (x0,y0, x1,y1, ...) pairs in world
   tile coordinates, start first, goal last (*out_len >= 1).
   RLK_E_NOTFOUND if the goal is unreachable or blocked.
   RLK_E_INVALID if a coordinate is outside the window or cap < *out_len.
   Asserted: exactly one cost source set, window of at most 1<<20 cells. */
rlk_err_t rlk_path_find(const rlk_path_cost_t *cost, rlk_path_scratch_t *scr,
                        int sx, int sy, int gx, int gy,
                        int *out, int cap, int *out_len);

/* Draw a path (as produced by rlk_path_find) onto a tilemap: each segment
   is brushed with rlk_tilemap_set_line (square brush of `width` tiles). */
void rlk_path_draw(rlk_tilemap_t *tm, const int *path, int n,
                   int width, rlk_tile_t tile);

#endif /* RLK_PATH_H */
