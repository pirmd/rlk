#ifndef RLK_HYDRO_H
#define RLK_HYDRO_H

/*
 * Hydrology derived fields, computed from a pre-built rlk_drain_t.
 * drain gives the flow topology (where water goes, how much accumulates:
 * filled/dir/acc); hydro adds the per-cell flow intensity that a renderer
 * or a simulation wants. One value per cell of the drain's lattice (w*h,
 * same geometry) -- not a separate grid.
 * Typical chain: rlk_drain_build() on the coarse elevation grid, then
 * rlk_hydro_build() on the drain; the wg stages (wg_drain, wg_hydro,
 * wg_rivers) then read both, per tile.
 *
 *   slope    per cell, D8 downslope on the filled surface, elevation units
 *            per tile: (filled[c] - filled[downstream]) / (cellsize * sqrt(2)
 *            if diagonal). 0 at outlets. Always >= 0 (Priority-Flood fills
 *            uphill). Used for slope-driven terrain: beaches, cliffs, mills.
 *   strahler per cell, Strahler (1957) stream order: the size class of the
 *            stream at that cell. Sources are order 1; when two or more
 *            upstreams of equal max order join, the order goes up by 1,
 *            otherwise it keeps the max. So 1-2 = headwater creeks, 5+ =
 *            large rivers -- used to grade river width. One reverse pass
 *            over drain->order.
 *   speed    per cell, Manning-Strickler estimate in relative units:
 *            v = (1/n) * R^(2/3) * sqrt(slope), R = width/2,
 *            width = min(1 + log2(acc/acc_min), cellsize) tiles (same width
 *            law as rlk_drain_channel_width), clamped >= 0.25.
 *            Used for flow simulation / rendering (rapids, current).
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

#include <stdint.h>

#include "drain.h"
#include "geom.h"

/* Hydrology data: caller-owned buffers, each optional (NULL = skip). */
typedef struct {
    rlk_lattice_t lattice;  /* same geometry as the drain it is built from */
    float    *slope;        /* D8 downslope, elevation units per tile, >= 0 */
    uint8_t  *strahler;     /* Strahler order, >= 1 */
    float    *speed;        /* Manning speed estimate, relative units, >= 0 */
} rlk_hydro_t;

/* --- PUBLIC API ---------------------------------------------------------- */

/* Fill h's per-cell buffers from a built drain (build = compute derived
   data). The hydro lattice must be identical to the drain's; each buffer is
   optional (NULL = skip). `scratch` is w*h uint16, used to accumulate the
   upstream orders during the Strahler pass (may be reused afterwards).
   acc_min >= 1 sets the reference drainage area for the width law. */
void rlk_hydro_build(rlk_hydro_t *h, const rlk_drain_t *d, uint32_t acc_min,
                     uint16_t *scratch);

#endif /* RLK_HYDRO_H */
