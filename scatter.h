#ifndef RLK_SCATTER_H
#define RLK_SCATTER_H

/* Stateless scatter (jittered grid): one point per lattice cell, derived
 * from (seed, i, j, salt) with rlk_hash_at. Any window recomputes
 * independently: no global state, no allocation. Flora, rocks, isolated
 * POIs. */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "geom.h"
#include "hash.h"
#include "mask.h"

/* Scattered point, world coords in tiles (float). */
typedef struct { float px, py; } rlk_scatter_point_t;

/* Jittered point of cell (i, j): out->px in [x, x+cs), out->py in [y, y+cs)
 * with x = lat.origin_x + i*cs, y = lat.origin_y + j*cs. salt and salt+1
 * are the two coordinate streams.
 *
 *   rlk_scatter_point_t p;
 *   rlk_scatter_at(&p, lat, seed, salt, i, j);
 */
static inline void
rlk_scatter_at(rlk_scatter_point_t *out, rlk_lattice_t lat, uint64_t seed,
               uint64_t salt, int i, int j) {
    assert(out);
    assert(i >= 0 && i < lat.w && j >= 0 && j < lat.h);
    float x = (float)(lat.origin_x + i * lat.cellsize);
    float y = (float)(lat.origin_y + j * lat.cellsize);
    float cs = (float)lat.cellsize;
    out->px = x + cs * rlk_hash_normf(rlk_hash_at(seed, i, j, salt));
    out->py = y + cs * rlk_hash_normf(rlk_hash_at(seed, i, j, salt + 1));
}

/* Scatter over a mask: one point per cell of lat with weight > 0, kept
 * with probability weight/255 (stream salt+2). Writes up to cap points to
 * out in row-major order (truncation is deterministic), returns the
 * count. m->lattice must equal lat. */
static inline int
rlk_scatter_mask(rlk_scatter_point_t *out, int cap, rlk_lattice_t lat,
                 const rlk_mask_t *m, uint64_t seed, uint64_t salt) {
    assert(out && m);
    assert(rlk_lattice_same(&lat, &m->lattice));
    assert(cap >= 0);
    int n = 0;
    for (int j = 0; j < lat.h; j++) {
        for (int i = 0; i < lat.w; i++) {
            int wgt = m->weights[(size_t)(j * lat.w + i)];
            if (wgt == 0)
                continue;
            float keep = rlk_hash_normf(rlk_hash_at(seed, i, j, salt + 2));
            if (keep * 255.0f >= (float)wgt)
                continue;
            if (n == cap)
                return n;
            rlk_scatter_at(&out[n], lat, seed, salt, i, j);
            n++;
        }
    }
    return n;
}

#endif /* RLK_SCATTER_H */
