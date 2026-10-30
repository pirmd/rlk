#ifndef RLK_HYDRO_H
#define RLK_HYDRO_H

/*
 * Hydrology derived fields on a coarse grid, computed from a pre-built
 * rlk_drain_t (Priority-Flood + D8 + accumulation). Companion of `drain`:
 * where `drain` stops at filled/dir/acc, `hydro` adds the hydraulic
 * quantities a river renderer or a simulation wants:
 *
 *   slope    per cell, D8 downslope in elevation units per tile:
 *            (filled[c] - filled[downstream]) / (cellsize * sqrt(2) if diagonal).
 *            0 at outlets. Always >= 0 (Priority-Flood fills uphill).
 *   strahler per cell, Strahler stream order: 1 at sources, max of the
 *            upstream orders, +1 when two or more upstreams reach that max.
 *            Computed in one reverse pass over drain->order (upstream first).
 *   speed    per cell, Manning estimate in relative units:
 *            v = (1/n) * R^(2/3) * sqrt(slope), R = width/2,
 *            width = min(1 + log2(acc/acc_min), cellsize) tiles (same
 *            hydraulic geometry as rlk_drain_river_sdf), clamped >= 0.25.
 *
 * Memory (caller-owned, no allocation): every output buffer is optional
 * (NULL = skip). slope 4B + strahler 1B + speed 4B = 9B per cell,
 * + 2B scratch (w*h uint16) during the Strahler pass.
 *
 * Usage (POD, designated initializer):
 *   float slope[W*H]; uint8_t strahler[W*H]; float speed[W*H];
 *   uint16_t scratch[W*H];
 *   rlk_hydro_t h = { .lattice = drain.lattice, .slope = slope,
 *                     .strahler = strahler, .speed = speed };
 *   rlk_hydro_build(&h, &drain, acc_min, scratch);
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "drain.h"
#include "geom.h"

/* Manning roughness coefficient used by the speed estimate. */
#define RLK_HYDRO_MANNING_N 0.03f

/* Hydrology data: caller-owned buffers, each optional (NULL = skip). */
typedef struct {
    rlk_lattice_t lattice;  /* same geometry as the drain it is built from */
    float    *slope;        /* D8 downslope, elevation units per tile, >= 0 */
    uint8_t  *strahler;     /* Strahler order, >= 1 */
    float    *speed;        /* Manning speed estimate, relative units, >= 0 */
} rlk_hydro_t;

/* --- PUBLIC API ---------------------------------------------------------- */

/* Compute the derived fields from a built drain. The hydro lattice must be
   identical to the drain's. `scratch` is w*h uint16, used to accumulate the
   upstream orders during the Strahler pass (may be reused afterwards).
   acc_min >= 1 sets the reference drainage area for the width law. */
void rlk_hydro_build(rlk_hydro_t *h, const rlk_drain_t *d, uint32_t acc_min,
                     uint16_t *scratch);

#endif /* RLK_HYDRO_H */
