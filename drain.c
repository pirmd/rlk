#include "drain.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "fmath.h"

/* Internal: filled value marking an unvisited cell during the flood.
   Unreachable through rlk_drain_quant() (clamped to 65534). */
#define RLK_DRAIN_UNVISITED  ((uint16_t)0xFFFF)

/* Maximum cells for single-pass processing. */
#define RLK_DRAIN_MAX_CELLS  (1 << 20)

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

/* Priority-Flood over the elevation grid. `filled` is required and doubles as
   the visited marker (initialized to RLK_DRAIN_UNVISITED). `dir` and `order`
   are optional outputs (NULL = skip). */
static rlk_err_t
drain_flood(const rlk_grid_t *elev, float sea, uint64_t *scratch,
            uint16_t *filled, uint8_t *dir, int *order) {
    const int w = elev->lattice.w, h = elev->lattice.h, n = w * h;
    float lo, hi;
    rlk_grid_minmax(elev, &lo, &hi);
    if (lo < 0.0f || hi >= RLK_DRAIN_SCALE)
        return RLK_E_INVALID;

    for (int c = 0; c < n; c++)
        filled[c] = RLK_DRAIN_UNVISITED;

    int heap_n = 0, popped = 0;
    uint64_t serial = 0;

    /* Seeds: the ocean and the edge of the grid, in scan order (deterministic FIFO). */
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int c = j * w + i;
            int edge = (i == 0 || j == 0 || i == w - 1 || j == h - 1);
            if (edge || elev->v[(size_t)c] < sea) {
                uint16_t q = rlk_drain_quant(elev->v[(size_t)c]);
                filled[c] = q;
                if (dir)
                    dir[c] = RLK_DRAIN_OUTLET;
                heap_push(scratch, &heap_n,
                          ((uint64_t)q << 40) | (serial++ << 20) | (uint64_t)c);
            }
        }
    }

    /* Flood from the lowest frontier cell. */
    while (heap_n > 0) {
        uint64_t key = heap_pop(scratch, &heap_n);
        int c = (int)(key & 0xFFFFFu);
        if (order)
            order[popped] = c;
        popped++;
        const int ci = c % w, cj = c / w;
        const uint16_t level = filled[c];

        for (int d = 0; d < 8; d++) {
            int dx, dy;
            rlk_drain_step(d, &dx, &dy);
            int ni = ci + dx, nj = cj + dy;
            if (ni < 0 || ni >= w || nj < 0 || nj >= h) continue;
            int nc = nj * w + ni;
            if (filled[nc] != RLK_DRAIN_UNVISITED) continue;
            uint16_t q = rlk_drain_quant(elev->v[(size_t)nc]);
            uint16_t nl = q > level ? q : level;  /* fill the pit up to the pass */
            filled[nc] = nl;
            if (dir)
                dir[nc] = (uint8_t)((d + 4) & 7);  /* back toward c: its downstream */
            heap_push(scratch, &heap_n,
                      ((uint64_t)nl << 40) | (serial++ << 20) | (uint64_t)nc);
        }
    }
    assert(popped == n);  /* the grid is 8-connected */
    return RLK_OK;
}

rlk_err_t
rlk_drain_remove_pits(uint16_t *out, const rlk_grid_t *elev,
                      float sea, uint64_t *scratch) {
    assert(out && elev && scratch);
    assert(elev->lattice.w >= 1 && elev->lattice.h >= 1);
    assert((int64_t)elev->lattice.w * (int64_t)elev->lattice.h <= RLK_DRAIN_MAX_CELLS);
    return drain_flood(elev, sea, scratch, out, NULL, NULL);
}

rlk_err_t
rlk_drain_build(rlk_drain_t *d, const rlk_grid_t *elev, float sea,
                uint64_t *scratch) {
    assert(d && elev && scratch);
    assert(d->filled && d->dir && d->acc && d->order);
    assert(d->lattice.w >= 1 && d->lattice.h >= 1);
    assert((int64_t)d->lattice.w * (int64_t)d->lattice.h <= RLK_DRAIN_MAX_CELLS);
    assert(rlk_lattice_same(&d->lattice, &elev->lattice));

    rlk_err_t err = drain_flood(elev, sea, scratch, d->filled, d->dir, d->order);
    if (err != RLK_OK)
        return err;

    /* Accumulate from sources to mouths: reverse of the pop order. */
    const int w = d->lattice.w, h = d->lattice.h, n = w * h;
    for (int c = 0; c < n; c++)
        d->acc[c] = 1u;
    for (int k = n - 1; k >= 0; k--) {
        int c = d->order[k];
        uint8_t dir = d->dir[c];
        if (dir == RLK_DRAIN_OUTLET) continue;
        int dx, dy;
        rlk_drain_step(dir, &dx, &dy);
        int t = (c / w + dy) * w + (c % w + dx);
        d->acc[t] += d->acc[c];
    }
    return RLK_OK;
}

float
rlk_drain_river_sdf(const rlk_drain_t *d, int x, int y, uint32_t acc_min) {
    assert(d && d->filled && d->dir && d->acc);
    assert(acc_min >= 1);
    const int w = d->lattice.w, h = d->lattice.h;
    const float cell = (float)d->lattice.cellsize;
    const int idx = rlk_lattice_index(&d->lattice, x, y);
    if (idx < 0)
        return RLK_DRAIN_FAR;
    const int ci = idx % w, cj = idx / w;

    const float px = (float)x + 0.5f;
    const float py = (float)y + 0.5f;

    float best = RLK_DRAIN_FAR;
    for (int dj = -1; dj <= 1; dj++) {
        const int j = cj + dj;
        if (j < 0 || j >= h) continue;
        for (int di = -1; di <= 1; di++) {
            const int i = ci + di;
            if (i < 0 || i >= w) continue;
            const size_t c = (size_t)j * (size_t)w + (size_t)i;
            const uint32_t acc = d->acc[c];
            if (acc < acc_min) continue;
            const uint8_t dir = d->dir[c];
            if (dir == RLK_DRAIN_OUTLET) continue;
            int dx, dy;
            rlk_drain_step(dir, &dx, &dy);
            const int ni = i + dx, nj = j + dy;
            if (ni < 0 || ni >= w || nj < 0 || nj >= h) continue;

            /* Segment: centre of (i,j) -> centre of (ni,nj), in world tiles. */
            const float ax = (float)d->lattice.origin_x + ((float)i + 0.5f) * cell;
            const float ay = (float)d->lattice.origin_y + ((float)j + 0.5f) * cell;
            const float vx = (float)dx * cell;
            const float vy = (float)dy * cell;

            /* Project (px,py) onto the segment. */
            const float ex = px - ax;
            const float ey = py - ay;
            const float vv = vx * vx + vy * vy;
            float t = vv > 0.0f ? (ex * vx + ey * vy) / vv : 0.0f;
            t = rlk_clampf(t, 0.0f, 1.0f);
            const float ox = ax + t * vx - px;
            const float oy = ay + t * vy - py;
            const float dist = sqrtf(ox * ox + oy * oy);

            /* Half-width: hydraulic geometry, capped at half a cell. */
            float lw = 1.0f + log2f((float)acc / (float)acc_min);
            if (lw > cell) lw = cell;
            const float halfw = 0.5f * lw;

            const float sdf = dist - halfw;
            if (sdf < best)
                best = sdf;
        }
    }
    return best;
}

rlk_err_t
rlk_drain_acc_mask(rlk_mask_t *m, const rlk_drain_t *d, uint32_t lo, uint32_t hi) {
    assert(m && d && d->acc);
    if (!rlk_lattice_same(&m->lattice, &d->lattice))
        return RLK_E_INVALID;
    const int n = d->lattice.w * d->lattice.h;
    for (int k = 0; k < n; k++)
        m->v[k] = (uint8_t)((d->acc[k] >= lo && d->acc[k] <= hi) ? 255 : 0);
    return RLK_OK;
}
