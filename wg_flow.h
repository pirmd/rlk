#ifndef RLK_WG_FLOW_H
#define RLK_WG_FLOW_H

/*
 * Drainage on a coarse elevation grid: depression filling, D8 flow directions
 * and flow accumulation. Works on any rlk_wg_grid_t of elevations.
 *
 * From a grid of elevations and a sea level:
 *   fill   per cell, the elevation after filling depressions (quantized).
 *          Where it is higher than the original elevation the cell is a LAKE.
 *   dir    per cell, the direction of its downstream neighbour (0..7, E SE S SW W NW N NE)
 *          or RLK_WG_FLOW_OUTLET for cells that drain out of the domain.
 *   acc    per cell, the number of cells draining through it, itself included
 *          (the "drainage area"; a river is where acc is large).
 *   order  cells sorted so that every cell comes AFTER its downstream neighbour.
 *
 * Algorithm: Priority-Flood (Barnes et al.). Flood from the outlets (sea + grid edge)
 * with a priority queue ordered by elevation. Cost: O(n log n).
 *
 * Determinism: elevations are quantized to 1/256 steps relative to the grid minimum
 * before the flood. Ties are broken FIFO, never by memory addresses.
 *
 * Memory (caller-owned, no allocation):
 *   fill 2B + dir 1B + acc 4B + order 4B = 11B persistent per cell
 *   + 8B scratch (w*h uint64) during build.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "err.h"
#include "wg_grid.h"

#define RLK_WG_FLOW_MAX_CELLS  (1 << 20)  /* Maximum cells for single-pass processing */
#define RLK_WG_FLOW_OUTLET     8          /* dir value: drains out of the domain */
#define RLK_WG_FLOW_UNSET      255        /* internal: not visited yet */
#define RLK_WG_FLOW_SCALE      256.0f     /* quantization: 1/256 elevation units */

/* Flow data: caller-owned buffers. */
typedef struct {
    int w, h;          /* same size as the grid it is built from */
    float lo;          /* grid minimum used for quantization */
    uint16_t *fill;    /* filled elevation, quantized: (e - lo) * RLK_WG_FLOW_SCALE */
    uint8_t *dir;      /* 0..7 downstream direction, RLK_WG_FLOW_OUTLET */
    uint32_t *acc;     /* drainage area in cells, >= 1 */
    int *order;        /* cells, downstream before upstream */
} rlk_wg_flow_t;

/* --- PUBLIC API ---------------------------------------------------------- */

/* Bind a flow to caller storage (w*h elements each). */
void rlk_wg_flow_init(rlk_wg_flow_t *f, int w, int h,
                      uint16_t *fill, uint8_t *dir, uint32_t *acc, int *order);

/* Build fill / dir / acc / order from the elevation grid. Cells with elevation < sea
   are the ocean; grid edge cells are outlets too. `scratch` holds w*h uint64.
   Returns RLK_OK on success, RLK_E_INVALID if elevation span >= 256. */
rlk_err_t rlk_wg_flow_build(rlk_wg_flow_t *f, const rlk_wg_grid_t *elev,
                            float sea, uint64_t *scratch);

/* --- HELPERS (static inline) --------------------------------------------- */

/* Quantize elevation to integer (1/256 steps, relative to the grid minimum). */
static inline uint16_t
rlk_wg_flow_quant(float e, float lo) {
    return (uint16_t)((e - lo) * RLK_WG_FLOW_SCALE);
}

/* Step of a direction: 0=E 1=SE 2=S 3=SW 4=W 5=NW 6=N 7=NE. */
static inline void
rlk_wg_flow_step(int d, int *dx, int *dy) {
    static const int8_t sx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int8_t sy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    assert(d >= 0 && d < 8);
    *dx = sx[d];
    *dy = sy[d];
}

/* Downstream cell of (i, j). Returns 0 for an outlet (outputs untouched). */
static inline int
rlk_wg_flow_downstream(const rlk_wg_flow_t *f, int i, int j, int *ni, int *nj) {
    assert(f && ni && nj);
    uint8_t d = f->dir[(size_t)j * (size_t)f->w + (size_t)i];
    if (d == RLK_WG_FLOW_OUTLET) return 0;
    static const int8_t sx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int8_t sy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    *ni = i + sx[d];
    *nj = j + sy[d];
    return 1;
}

/* Filled elevation of a cell, back in elevation units. */
static inline float
rlk_wg_flow_fill_elev(const rlk_wg_flow_t *f, int i, int j) {
    assert(f);
    return f->lo + (float)f->fill[(size_t)j * (size_t)f->w + (size_t)i] / RLK_WG_FLOW_SCALE;
}

/* Depth of the depression a cell sits in: filled minus original elevation,
   in elevation units (0 on slopes and at sea). `elev` must be the grid the
   flow was built from. */
static inline float
rlk_wg_flow_lake_depth(const rlk_wg_flow_t *f, const rlk_wg_grid_t *elev,
                       float sea, int i, int j) {
    assert(f && elev);
    float e = elev->v[(size_t)j * (size_t)elev->w + (size_t)i];
    if (e < sea) return 0.0f;
    uint16_t q = (uint16_t)((e - f->lo) * RLK_WG_FLOW_SCALE);
    uint16_t z = f->fill[(size_t)j * (size_t)f->w + (size_t)i];
    return z > q ? (float)(z - q) / RLK_WG_FLOW_SCALE : 0.0f;
}

/* 1 if the cell is part of a lake: its basin was filled by at least `min_depth`. */
static inline int
rlk_wg_flow_is_lake(const rlk_wg_flow_t *f, const rlk_wg_grid_t *elev,
                    float sea, float min_depth, int i, int j) {
    float d = rlk_wg_flow_lake_depth(f, elev, sea, i, j);
    return d > 0.0f && d >= min_depth;
}

#endif /* RLK_WG_FLOW_H */
