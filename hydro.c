#include "hydro.h"

#include <math.h>

/* Downstream cell index of c, or -1 for an outlet. */
static inline int
downstream_cell(const rlk_drain_t *d, int c) {
    const int w = d->lattice.w;
    const uint8_t dir = d->dir[c];
    if (dir == RLK_DRAIN_OUTLET) return -1;
    int dx, dy;
    rlk_drain_step(dir, &dx, &dy);
    const int ni = (c % w) + dx, nj = (c / w) + dy;
    return nj * w + ni;
}

void
rlk_hydro_build(rlk_hydro_t *h, const rlk_drain_t *d, uint32_t acc_min,
                uint16_t *scratch) {
    assert(h && d && scratch);
    assert(d->filled && d->dir && d->acc && d->order);
    assert(h->slope || h->strahler || h->speed);
    assert(h->lattice.w >= 1 && h->lattice.h >= 1);
    assert(rlk_lattice_same(&h->lattice, &d->lattice));
    assert(acc_min >= 1);

    const int w = h->lattice.w, hgt = h->lattice.h, n = w * hgt;
    const float cell = (float)h->lattice.cellsize;
    const float inv_scale = 1.0f / RLK_DRAIN_SCALE;

    /* Pass 1: per-cell slope and Manning speed (independent of topology). */
    for (int c = 0; c < n; c++) {
        const int t = downstream_cell(d, c);
        float slope = 0.0f;
        if (t >= 0) {
            const int df = (int)d->filled[c] - (int)d->filled[t];
            const int dx = t % w - c % w, dy = t / w - c / w;
            const float dist = cell * ((dx != 0 && dy != 0) ? 1.41421356f : 1.0f);
            slope = (float)df * inv_scale / dist;
        }
        if (h->slope)
            h->slope[c] = slope;
        if (h->speed) {
            /* Width law shared with rlk_drain_river_sdf, clamped for
               headwater cells below acc_min. */
            float lw = 1.0f + log2f((float)d->acc[c] / (float)acc_min);
            if (lw < 0.25f) lw = 0.25f;
            if (lw > cell) lw = cell;
            const float r = 0.5f * lw;  /* hydraulic radius ~ half width */
            h->speed[c] = (1.0f / RLK_HYDRO_MANNING_N)
                          * powf(r, 2.0f / 3.0f) * sqrtf(slope);
        }
    }

    /* Pass 2: Strahler order. Reverse of the pop order, so every upstream
       cell is visited before its downstream cell; scratch[c] packs the
       running (max upstream order << 8) | count. */
    if (h->strahler) {
        for (int c = 0; c < n; c++)
            scratch[c] = 0;
        for (int k = n - 1; k >= 0; k--) {
            const int c = d->order[k];
            const unsigned mx = (unsigned)scratch[c] >> 8;
            const unsigned cnt = (unsigned)scratch[c] & 0xFFu;
            const uint8_t s = (uint8_t)(cnt >= 2 ? mx + 1 : (cnt == 1 ? mx : 1));
            h->strahler[c] = s;
            const int t = downstream_cell(d, c);
            if (t < 0) continue;
            unsigned tmx = (unsigned)scratch[t] >> 8;
            unsigned tcnt = (unsigned)scratch[t] & 0xFFu;
            if (s > tmx) { tmx = s; tcnt = 1; }
            else if (s == tmx) { tcnt++; }
            scratch[t] = (uint16_t)((tmx << 8) | tcnt);
        }
    }
}
