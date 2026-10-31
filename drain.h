#ifndef RLK_DRAIN_H
#define RLK_DRAIN_H

/*
 * Drainage on a coarse elevation grid: depression filling, D8 flow
 * directions, flow accumulation, signed distance to the flowline network
 * (high-accumulation channels). Works on any rlk_grid_t of elevations.
 * Named `drain` (not `flow`) to stay clear of pathfinding "flow fields".
 * See hydro.h for derived hydraulic fields (slope / Strahler / Manning
 * speed) computed from a built drain.
 *
 * From a grid of elevations and an outlet level:
 *   filled  per cell, elevation after depression filling (quantized); where
 *           higher than the original, the cell sits in a filled pit (what
 *           that means -- e.g. a lake -- is the caller's interpretation).
 *   dir     per cell, D8 direction of the downstream neighbour
 *           (RLK_DRAIN_E .. RLK_DRAIN_NE), or RLK_DRAIN_OUTLET (domain border
 *           or below outlet_level).
 *   acc     per cell, flow accumulation: cells draining through it, itself
 *           included (>= 1, a plain count). High acc = flowline.
 *   order   cells sorted so that each comes AFTER its downstream neighbour.
 *
 * Algorithms: Priority-Flood (Barnes et al. 2014) for filling; the flood
 * tree gives the D8 directions; accumulation is a reverse pass over `order`;
 * flowline_sdf is a local point-to-segment SDF (3x3 window, O(1) per tile)
 * with a log-width law (width ~ log2(acc)). O(n log n) overall.
 *
 * Determinism: elevations are quantized to 1/256 (filled = e *
 * RLK_DRAIN_SCALE); ties are broken FIFO, never by address. Valid elevation
 * range [0, 256); rlk_drain_build() returns RLK_E_INVALID outside it.
 *
 * Memory (caller-owned, no allocation): 11B persistent per cell
 * (filled 2 + dir 1 + acc 4 + order 4) + 8B scratch (w*h uint64).
 *
 * Usage (POD, designated initializer):
 *   uint16_t filled[W*H]; uint8_t dir[W*H]; uint32_t acc[W*H];
 *   int order[W*H]; uint64_t scratch[W*H];
 *   rlk_drain_t d = { .lattice = elev.lattice, .filled = filled, .dir = dir,
 *                     .acc = acc, .order = order };
 *   rlk_drain_build(&d, &elev, outlet_level, scratch);
 */

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "err.h"
#include "geom.h"
#include "grid.h"

/* D8 directions: values of rlk_drain_t.dir (0..7) and of rlk_drain_step. */
enum {
    RLK_DRAIN_E = 0, RLK_DRAIN_SE, RLK_DRAIN_S, RLK_DRAIN_SW,
    RLK_DRAIN_W, RLK_DRAIN_NW, RLK_DRAIN_N, RLK_DRAIN_NE
};
#define RLK_DRAIN_OUTLET     8          /* dir value: drains out of the domain */
#define RLK_DRAIN_SCALE      256.0f     /* quantization: 1/256 elevation units */
#define RLK_DRAIN_FAR        1.0e9f     /* flowline_sdf: no network segment nearby */

/* Drainage data: caller-owned buffers. */
typedef struct {
    rlk_lattice_t lattice;  /* same geometry as the elevation grid it is built from */
    uint16_t *filled;       /* filled elevation, quantized: e * RLK_DRAIN_SCALE */
    uint8_t  *dir;          /* D8 downstream direction 0..7, or RLK_DRAIN_OUTLET */
    uint32_t *acc;          /* flow accumulation: cells draining through this one, >= 1 */
    int      *order;        /* cells, downstream before upstream */
} rlk_drain_t;

/* --- PUBLIC API ---------------------------------------------------------- */

/* Depression filling only ("fill sinks"). out: w*h quantized filled elevations
   (same encoding as rlk_drain_t.filled). `elev` and `outlet_level` as in build. */
rlk_err_t rlk_drain_remove_pits(uint16_t *out, const rlk_grid_t *elev,
                                float outlet_level, uint64_t *scratch);

/* Full drainage: fills filled, dir (D8), acc, order. The drain lattice must be
   identical to the elevation grid's. Cells with elevation < outlet_level are
   outlets (they drain out of the domain): pass the sea level to flood from the
   ocean, or a value below the grid minimum for edge-only outlets. Returns
   RLK_OK on success, RLK_E_INVALID if elevations are outside [0, 256). */
rlk_err_t rlk_drain_build(rlk_drain_t *d, const rlk_grid_t *elev,
                          float outlet_level, uint64_t *scratch);

/* Signed distance, in tiles, from tile (x, y) to the flowline network
   (segments "cell centre -> downstream centre" of cells with acc >= acc_min;
   half-width 0.5 * min(1 + log2(acc/acc_min), cellsize)): < 0 in the
   channel, 0 on the bank, > 0 outside; RLK_DRAIN_FAR if none nearby.
   Only the 3x3 cells around the query are tested: O(1) per tile, pure
   function of (d, x, y), streamable. Operational definition, not a terrain
   type: what a flowline means on the map is the caller's call. */
float rlk_drain_flowline_sdf(const rlk_drain_t *d, int x, int y, uint32_t acc_min);

/* --- HELPERS (static inline) --------------------------------------------- */

/* Quantize elevation to integer (1/256 steps, absolute). At most 65534:
   0xFFFF is reserved internally as the "unvisited" sentinel. */
static inline uint16_t
rlk_drain_quant(float e) {
    float q = e * RLK_DRAIN_SCALE;
    return q >= 65535.0f ? (uint16_t)65534 : (uint16_t)q;
}

/* Step (dx, dy) of a D8 direction RLK_DRAIN_E .. RLK_DRAIN_NE. */
static inline void
rlk_drain_step(int d, int *dx, int *dy) {
    static const int8_t sx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int8_t sy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    assert(d >= 0 && d < 8);
    *dx = sx[d];
    *dy = sy[d];
}

/* Log-width law: channel width = 1 + log2(acc/acc_min) tiles, capped at
   cellsize. Shared by rlk_drain_flowline_sdf and the hydro speed estimate. */
static inline float
rlk_drain_channel_width(uint32_t acc, uint32_t acc_min, float cellsize) {
    assert(acc_min >= 1);
    float lw = 1.0f + log2f((float)acc / (float)acc_min);
    return lw > cellsize ? cellsize : lw;
}

/* Downstream cell index of c, or -1 for an outlet. */
static inline int
rlk_drain_downstream(const rlk_drain_t *d, int c) {
    assert(d);
    const int w = d->lattice.w;
    const uint8_t dir = d->dir[(size_t)c];
    if (dir == RLK_DRAIN_OUTLET) return -1;
    int dx, dy;
    rlk_drain_step(dir, &dx, &dy);
    return (c / w + dy) * w + (c % w + dx);
}

/* Depth of the depression a cell sits in: filled minus original elevation,
   in elevation units (0 where nothing was filled). Purely geometric -- what a
   filled cell means (e.g. a lake above sea level) is the caller's call.
   `elev` must be the grid the drain was built from. */
static inline float
rlk_drain_fill_depth(const rlk_drain_t *d, const rlk_grid_t *elev,
                     int i, int j) {
    assert(d && elev);
    const float e = elev->values[(size_t)j * (size_t)elev->lattice.w + (size_t)i];
    const uint16_t q = rlk_drain_quant(e);
    const uint16_t z = d->filled[(size_t)j * (size_t)d->lattice.w + (size_t)i];
    return z > q ? (float)(z - q) / RLK_DRAIN_SCALE : 0.0f;
}

#endif /* RLK_DRAIN_H */
