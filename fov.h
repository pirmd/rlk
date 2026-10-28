#ifndef RLK_FOV_H
#define RLK_FOV_H

/* layer: 2, domain: spatial */

/*
 * Field of view by recursive shadowcasting on a tile-aligned window
 * (rlk_lattice_t with cellsize == 1). 8 octants, deterministic,
 * zero-allocation: the caller provides the opaque and visible buffers
 * (w*h bytes each).
 *
 *   uint8_t opaque[W*H];    != 0: blocks sight (the cell itself is still seen)
 *   uint8_t visible[W*H];   out: 1 = visible from (ox, oy)
 *   rlk_fov_compute(&win, opaque, ox, oy, radius, visible);
 *
 * `visible` is cleared on entry. radius is in tiles (euclidean);
 * radius <= 0 means no distance limit (the window still bounds the view).
 */

#include <assert.h>
#include <stdint.h>

#include "geom.h"

/* Compute the field of view from (ox, oy) (world tile coordinates) over the
   window. opaque may be NULL (nothing blocks). visible is reset to 0, then
   set to 1 for every visible cell (the origin included). */
void rlk_fov_compute(const rlk_lattice_t *win, const uint8_t *opaque,
                     int ox, int oy, int radius, uint8_t *visible);

#endif /* RLK_FOV_H */
