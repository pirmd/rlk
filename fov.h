#ifndef RLK_FOV_H
#define RLK_FOV_H

/*
 * Field of view by recursive shadowcasting on a tile-aligned window
 * (rlk_lattice_t with cellsize == 1). 8 octants, deterministic,
 * zero-allocation: the caller provides the opaque and visible buffers
 * (w*h bytes each).
 *
 *   uint8_t opaque[W*H];    != 0: blocks sight (the cell itself is still seen)
 *   uint8_t visible[W*H];   out: 1 = visible from (ox, oy)
 *   rlk_fov_compute(visible, &win, opaque, ox, oy, radius);
 *
 * `visible` is cleared on entry. radius is in tiles (euclidean);
 * radius <= 0 means no distance limit (the window still bounds the view).
 */

#include <stdint.h>

#include "geom.h"

/* Compute the field of view from (ox, oy) (world tile coordinates) into
   visible: reset to 0, then set to 1 for every visible cell (the origin
   included). opaque may be NULL (nothing blocks). Deterministic, but not
   guaranteed symmetric (A seeing B does not imply B sees A). */
void rlk_fov_compute(uint8_t *visible, const rlk_lattice_t *win,
                     const uint8_t *opaque, int ox, int oy, int radius);

#endif /* RLK_FOV_H */
