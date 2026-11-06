#ifndef RLK_PATH_DISTANCE_H
#define RLK_PATH_DISTANCE_H

/*
 * Multi-source Dijkstra distance field on the same cost model as
 * rlk_path_find (rlk_path_cost_t). All sources are seeded with g = 0 and
 * the window is flooded once: g[cell] is the cheapest cost from any
 * source, parent[cell] the next cell on that cheapest route (gradient
 * descent toward the nearest source). Typical uses: flow fields (one
 * flood per goal instead of one A* per agent), spawn weighting, heat or
 * danger maps, O(1) "nearest source" queries.
 * Deterministic: heap ties are broken by cell index, never by memory
 * addresses. Zero-allocation: the caller provides the scratch buffers.
 *
 * rlk_path_distance_build resets the whole window (O(w*h)) on every call:
 * pass small windows around the area of interest, not whole maps.
 *
 *   int g[W*H], parent[W*H], heap[W*H], heap_pos[W*H];
 *   rlk_path_distance_scratch_t scr = { g, parent, heap, heap_pos };
 *   int sources[2 * N];   (x0,y0, x1,y1, ...) in world tile coords
 *   rlk_err_t e = rlk_path_distance_build(&cost, &scr, sources, N);
 *   int path[2 * W*H];
 *   int n;
 *   e = rlk_path_distance_find(&cost, &scr, x, y, path, W*H, &n);
 */

#include <limits.h>

#include "err.h"
#include "geom.h"
#include "path.h"

/* Unreachable sentinel stored in g[] (same bound argument as rlk_path_find:
   a route visits each cell at most once, a step costs at most 3*255, and
   the window is asserted to at most 1<<20 cells, so g < INT_MAX/2). */
#define RLK_PATH_DISTANCE_INF (INT_MAX / 2)

/* Scratch buffers, caller-owned: four int arrays of w*h each. */
typedef struct {
    int *g;          /* cheapest cost from any source; RLK_PATH_DISTANCE_INF = unreachable */
    int *parent;     /* next cell toward the nearest source, -1 = source or unreachable */
    int *heap;       /* binary heap of cell indices (open list) */
    int *heap_pos;   /* heap position per cell, -1 = not in the heap */
} rlk_path_distance_scratch_t;

/* Flood the window from nsrc sources (sources = (x0,y0, x1,y1, ...) in
   world tile coords). Returns RLK_E_INVALID if a source lies outside the
   window; blocked sources are skipped. RLK_OK even when every cell stays
   unreachable (nsrc == 0 included).
   Asserted: exactly one cost source set, window of at most 1<<20 cells. */
rlk_err_t rlk_path_distance_build(const rlk_path_cost_t *cost, rlk_path_distance_scratch_t *scr,
                                   const int *sources, int nsrc);

/* Find the route from (x,y) to its nearest source by descending parent[].
   On RLK_OK, out holds *out_len points as (x0,y0, x1,y1, ...) pairs in
   world tile coordinates, start first, source last (*out_len >= 1).
   RLK_E_NOTFOUND if (x,y) is unreachable, RLK_E_INVALID if (x,y) is
   outside the window or cap < *out_len. */
rlk_err_t rlk_path_distance_find(const rlk_path_cost_t *cost, const rlk_path_distance_scratch_t *scr,
                                  int x, int y, int *out, int cap, int *out_len);

#endif /* RLK_PATH_DISTANCE_H */
