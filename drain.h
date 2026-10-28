#ifndef RLK_DRAIN_H
#define RLK_DRAIN_H

/* layer: 2, domain: terrain */

/*
 * Drainage on a coarse elevation grid: depression filling, D8 flow
 * directions, flow accumulation, and signed distance to the river network.
 * Works on any rlk_grid_t of elevations. Named `drain` (not `flow`) to stay
 * clear of the "flow fields" of pathfinding.
 *
 * From a grid of elevations and a sea level:
 *   filled  per cell, the elevation after filling depressions (quantized).
 *           Where it is higher than the original elevation the cell is a LAKE.
 *   dir     per cell, the direction of its downstream neighbour (0..7, E SE S
 *           SW W NW N NE) or RLK_DRAIN_OUTLET for cells that drain out of the
 *           domain.
 *   acc     per cell, the number of cells draining through it, itself included
 *           (the "drainage area"; a river is where acc is large).
 *   order   cells sorted so that every cell comes AFTER its downstream neighbour.
 *
 * Algorithm: Priority-Flood (Barnes et al.). Flood from the outlets (sea + grid
 * edge) with a priority queue ordered by elevation. Cost: O(n log n).
 *
 * Determinism: elevations are quantized to 1/256 steps (absolute:
 * filled = e * RLK_DRAIN_SCALE) before the flood. Ties are broken FIFO, never
 * by memory addresses. Valid elevation range: [0, 256) — rlk elevations are
 * normalized [0, 1]; rlk_drain_build() returns RLK_E_INVALID outside it.
 *
 * Memory (caller-owned, no allocation):
 *   filled 2B + dir 1B + acc 4B + order 4B = 11B persistent per cell
 *   + 8B scratch (w*h uint64) during build.
 *
 * Usage (POD, designated initializer):
 *   uint16_t filled[W*H]; uint8_t dir[W*H]; uint32_t acc[W*H];
 *   int order[W*H]; uint64_t scratch[W*H];
 *   rlk_drain_t d = { .lattice = elev.lattice, .filled = filled, .dir = dir,
 *                     .acc = acc, .order = order };
 *   rlk_drain_build(&d, &elev, sea, scratch);
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "err.h"
#include "geom.h"
#include "grid.h"
#include "mask.h"

#define RLK_DRAIN_MAX_CELLS  (1 << 20)  /* Maximum cells for single-pass processing */
#define RLK_DRAIN_OUTLET     8          /* dir value: drains out of the domain */
#define RLK_DRAIN_SCALE      256.0f     /* quantization: 1/256 elevation units */
#define RLK_DRAIN_FAR        1.0e9f     /* river_sdf: no network segment nearby */
/* number of uint64_t elements to provide in scratch (remove_pits and build) */
#define RLK_DRAIN_SCRATCH(w, h)  ((size_t)(w) * (size_t)(h))

/* Drainage data: caller-owned buffers. */
typedef struct {
    rlk_lattice_t lattice;  /* same geometry as the elevation grid it is built from */
    uint16_t *filled;       /* filled elevation, quantized: e * RLK_DRAIN_SCALE */
    uint8_t  *dir;          /* 0..7 downstream direction, RLK_DRAIN_OUTLET */
    uint32_t *acc;          /* drainage area in cells, >= 1 */
    int      *order;        /* cells, downstream before upstream */
} rlk_drain_t;

/* --- PUBLIC API ---------------------------------------------------------- */

/* Depression filling only ("fill sinks"). out: w*h quantized filled elevations
   (same encoding as rlk_drain_t.filled). `elev` and `sea` as in build. */
rlk_err_t rlk_drain_remove_pits(uint16_t *out, const rlk_grid_t *elev,
                                float sea, uint64_t *scratch);

/* Full drainage: fills filled, dir (D8), acc, order. The drain lattice must be
   identical to the elevation grid's. Returns RLK_OK on success,
   RLK_E_INVALID if elevations are outside [0, 256). */
rlk_err_t rlk_drain_build(rlk_drain_t *d, const rlk_grid_t *elev, float sea,
                          uint64_t *scratch);

/* Signed distance, in tiles, from tile (x, y) to the river network:
   < 0 in the channel, 0 on the bank, > 0 outside. RLK_DRAIN_FAR if no network
   segment is nearby. The network is the set of segments "cell centre ->
   downstream cell centre" for cells with acc >= acc_min; a segment's
   half-width is 0.5 * min(1 + log2(acc / acc_min), cellsize) tiles, so a
   channel never exceeds cellsize tiles wide. Only the 3x3 cells around the
   one containing (x, y) are tested (at most 9 segments): constant cost per
   tile. Pure function of (d, x, y): no allocation, no side effect, evaluable
   tile by tile in any order (streamable). */
float rlk_drain_river_sdf(const rlk_drain_t *d, int x, int y, uint32_t acc_min);

/* Mask from accumulation: 255 if lo <= acc[c] <= hi, else 0.
   The mask geometry is the drain geometry. */
rlk_err_t rlk_drain_acc_mask(rlk_mask_t *m, const rlk_drain_t *d,
                             uint32_t lo, uint32_t hi);

/* --- HELPERS (static inline) --------------------------------------------- */

/* Quantize elevation to integer (1/256 steps, absolute). At most 65534:
   0xFFFF is reserved internally as the "unvisited" sentinel. */
static inline uint16_t
rlk_drain_quant(float e) {
    float q = e * RLK_DRAIN_SCALE;
    return q >= 65535.0f ? (uint16_t)65534 : (uint16_t)q;
}

/* Step of a direction: 0=E 1=SE 2=S 3=SW 4=W 5=NW 6=N 7=NE. */
static inline void
rlk_drain_step(int d, int *dx, int *dy) {
    static const int8_t sx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int8_t sy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    assert(d >= 0 && d < 8);
    *dx = sx[d];
    *dy = sy[d];
}

/* Downstream cell of (i, j). Returns 0 for an outlet (outputs untouched). */
static inline int
rlk_drain_downstream(const rlk_drain_t *d, int i, int j, int *ni, int *nj) {
    assert(d && ni && nj);
    uint8_t dir = d->dir[(size_t)j * (size_t)d->lattice.w + (size_t)i];
    if (dir == RLK_DRAIN_OUTLET) return 0;
    int dx, dy;
    rlk_drain_step(dir, &dx, &dy);
    *ni = i + dx;
    *nj = j + dy;
    return 1;
}

/* Filled elevation of a cell, back in elevation units. */
static inline float
rlk_drain_fill_elev(const rlk_drain_t *d, int i, int j) {
    assert(d);
    return (float)d->filled[(size_t)j * (size_t)d->lattice.w + (size_t)i]
           / RLK_DRAIN_SCALE;
}

/* Depth of the depression a cell sits in: filled minus original elevation,
   in elevation units (0 on slopes and at sea). `elev` must be the grid the
   drain was built from. */
static inline float
rlk_drain_lake_depth(const rlk_drain_t *d, const rlk_grid_t *elev,
                     float sea, int i, int j) {
    assert(d && elev);
    float e = elev->v[(size_t)j * (size_t)elev->lattice.w + (size_t)i];
    if (e < sea) return 0.0f;
    uint16_t q = rlk_drain_quant(e);
    uint16_t z = d->filled[(size_t)j * (size_t)d->lattice.w + (size_t)i];
    return z > q ? (float)(z - q) / RLK_DRAIN_SCALE : 0.0f;
}

/* 1 if the cell is part of a lake: its basin was filled by at least `min_depth`. */
static inline int
rlk_drain_is_lake(const rlk_drain_t *d, const rlk_grid_t *elev,
                  float sea, float min_depth, int i, int j) {
    float depth = rlk_drain_lake_depth(d, elev, sea, i, j);
    return depth > 0.0f && depth >= min_depth;
}

#endif /* RLK_DRAIN_H */
