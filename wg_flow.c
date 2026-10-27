#include "wg_flow.h"

#include <assert.h>
#include <stdint.h>

/* Binary min-heap of packed 64-bit keys.
   key = level << 40 | serial << 20 | cell : ordered by level, then FIFO. */

static inline void
heap_push(uint64_t *hp, int *n, uint64_t key) {
    int i = (*n)++;
    while (i > 0) {
        int p = (i - 1) >> 1;
        if (hp[p] <= key) break;
        hp[i] = hp[p];
        i = p;
    }
    hp[i] = key;
}

static inline uint64_t
heap_pop(uint64_t *hp, int *n) {
    uint64_t top = hp[0];
    uint64_t key = hp[--(*n)];
    int m = *n, i = 0;
    if (m > 0) {
        for (;;) {
            int c = 2 * i + 1;
            if (c >= m) break;
            if (c + 1 < m && hp[c + 1] < hp[c]) c++;
            if (hp[c] >= key) break;
            hp[i] = hp[c];
            i = c;
        }
        hp[i] = key;
    }
    return top;
}

rlk_err_t
rlk_wg_flow_build(rlk_wg_flow_t *f, const rlk_grid_t *elev,
                  float sea, uint64_t *scratch) {
    assert(f && elev && scratch);
    assert(f->fill && f->dir && f->acc && f->order);
    assert(f->w >= 1 && f->h >= 1);
    assert((int64_t)f->w * (int64_t)f->h <= RLK_WG_FLOW_MAX_CELLS);
    assert(f->w == elev->lattice.w && f->h == elev->lattice.h);

    const int w = f->w, h = f->h, n = w * h;
    float lo, hi;
    rlk_grid_minmax(elev, &lo, &hi);
    if (hi - lo >= 256.0f) return RLK_E_INVALID;
    f->lo = lo;

    for (int c = 0; c < n; c++) {
        f->dir[c] = RLK_WG_FLOW_UNSET;
        f->acc[c] = 1u;
    }

    int heap_n = 0, popped = 0;
    uint64_t serial = 0;

    /* Seeds: the ocean and the edge of the grid, in scan order (deterministic FIFO). */
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int c = j * w + i;
            int edge = (i == 0 || j == 0 || i == w - 1 || j == h - 1);
            if (edge || elev->v[(size_t)c] < sea) {
                uint16_t q = rlk_wg_flow_quant(elev->v[(size_t)c], lo);
                f->dir[c] = RLK_WG_FLOW_OUTLET;
                f->fill[c] = q;
                heap_push(scratch, &heap_n,
                          ((uint64_t)q << 40) | (serial++ << 20) | (uint64_t)c);
            }
        }
    }

    /* Flood from the lowest frontier cell. */
    while (heap_n > 0) {
        uint64_t key = heap_pop(scratch, &heap_n);
        int c = (int)(key & 0xFFFFFu);
        f->order[popped++] = c;
        const int ci = c % w, cj = c / w;
        const uint16_t level = f->fill[c];

        for (int d = 0; d < 8; d++) {
            int dx, dy;
            rlk_wg_flow_step(d, &dx, &dy);
            int ni = ci + dx, nj = cj + dy;
            if (ni < 0 || ni >= w || nj < 0 || nj >= h) continue;
            int nc = nj * w + ni;
            if (f->dir[nc] != RLK_WG_FLOW_UNSET) continue;
            uint16_t q = rlk_wg_flow_quant(elev->v[(size_t)nc], lo);
            uint16_t nl = q > level ? q : level;  /* fill the pit up to the pass */
            f->fill[nc] = nl;
            f->dir[nc] = (uint8_t)((d + 4) & 7);  /* back toward c: its downstream */
            heap_push(scratch, &heap_n,
                      ((uint64_t)nl << 40) | (serial++ << 20) | (uint64_t)nc);
        }
    }
    assert(popped == n);  /* the grid is 8-connected */

    /* Accumulate from sources to mouths: reverse of the pop order. */
    for (int k = n - 1; k >= 0; k--) {
        int c = f->order[k];
        uint8_t d = f->dir[c];
        if (d == RLK_WG_FLOW_OUTLET) continue;
        int dx, dy;
        rlk_wg_flow_step(d, &dx, &dy);
        int t = (c / w + dy) * w + (c % w + dx);
        f->acc[t] += f->acc[c];
    }
    f->built = 1;
    return RLK_OK;
}
