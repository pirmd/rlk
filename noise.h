#ifndef RLK_NOISE_H
#define RLK_NOISE_H

/* Deterministic 2D noise, pure functions of (seed, x, y), no allocation:
 * value noise, fBm, cellular (Worley F1/F2). Outputs in [0,1) except the
 * cellular distances. */

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>

#include "fmath.h"
#include "hash.h"

/* FBM octave offsets to decorrelate layers */
#define RLK_NOISE_FBM_OCTAVE_OFFSET_X 37.2173f
#define RLK_NOISE_FBM_OCTAVE_OFFSET_Y 91.7231f

/* Value noise state. Seed fully determines output. */
typedef struct {
    uint64_t seed;
} rlk_noise_t;

/* Hash of the integer lattice point (ix, iy) with the seed, mapped to [0,1). */
static inline float
rlk_noise_point_hash(const rlk_noise_t *n, int ix, int iy) {
    assert(n);
    return rlk_hash_normf(rlk_hash_at(n->seed, ix, iy, 0));
}

/* Value noise at (x,y) in [0,1). */
static inline float
rlk_noise_value(const rlk_noise_t *n, float x, float y) {
    assert(n);
    assert(x == x && y == y);
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    float fx = x - (float)x0;
    float fy = y - (float)y0;

    float v00 = rlk_noise_point_hash(n, x0,     y0);
    float v10 = rlk_noise_point_hash(n, x0 + 1, y0);
    float v01 = rlk_noise_point_hash(n, x0,     y0 + 1);
    float v11 = rlk_noise_point_hash(n, x0 + 1, y0 + 1);

    float sx = rlk_smoothstep(fx);
    float sy = rlk_smoothstep(fy);
    float a = rlk_lerp(v00, v10, sx);
    float b = rlk_lerp(v01, v11, sx);
    return rlk_lerp(a, b, sy);
}

/* Fractal Brownian Motion. Sum of `octaves` noise layers. Output in [0,1). */
static inline float
rlk_noise_fbm(const rlk_noise_t *n, float x, float y,
              int octaves, float freq, float gain) {
    assert(n);
    assert(octaves > 0);
    assert(gain > 0.0f && gain < 1.0f);
    float amp  = 1.0f;
    float sum  = 0.0f;
    float norm = 0.0f;
    float f    = freq;
    for (int o = 0; o < octaves; o++) {
        float ox = (float)o * RLK_NOISE_FBM_OCTAVE_OFFSET_X;
        float oy = (float)o * RLK_NOISE_FBM_OCTAVE_OFFSET_Y;
        sum  += amp * rlk_noise_value(n, x * f + ox, y * f + oy);
        norm += amp;
        amp  *= gain;
        f    *= 2.0f;
    }
    return sum / norm;
}

/* Worley / Voronoi cellular noise: feature points of a lattice of cells with
   side `cell_size`, one point jittered inside each cell. Pure function of
   (seed, x, y): deterministic, no allocation. */
typedef struct {
    uint64_t id;   /* identity (hash) of the closest feature point */
    int cx, cy;    /* cell of the closest feature point */
    float d1;      /* distance to the closest feature point (F1) */
    float d2;      /* distance to the second closest feature point (F2) */
} rlk_noise_cellular_t;

/* Closest feature points at (x, y): scans the 5x5 cells around the
   containing cell, which provably holds F1/F2 for jitter in [0.15, 0.85]. */
static inline void
rlk_noise_cellular(const rlk_noise_t *n, float x, float y, float cell_size,
               rlk_noise_cellular_t *out) {
    assert(n);
    assert(out);
    assert(cell_size > 0.0f);
    assert(x == x && y == y);
    const int bx = (int)floorf(x / cell_size);
    const int by = (int)floorf(y / cell_size);
    float d1 = FLT_MAX, d2 = FLT_MAX;
    uint64_t id = 0;
    int cx = bx, cy = by;
    for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
            const int ccx = bx + i;
            const int ccy = by + j;
            const uint64_t h = rlk_hash_combine2(n->seed, ccx, ccy);
            const float fx = 0.15f + 0.70f * rlk_hash_normf(rlk_hash_sub(h, 1));
            const float fy = 0.15f + 0.70f * rlk_hash_normf(rlk_hash_sub(h, 2));
            const float qx = ((float)ccx + fx) * cell_size - x;
            const float qy = ((float)ccy + fy) * cell_size - y;
            const float d = rlk_dist_euclideanf(0.0f, 0.0f, qx, qy);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                id = h;
                cx = ccx;
                cy = ccy;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    out->id = id;
    out->cx = cx;
    out->cy = cy;
    out->d1 = d1;
    out->d2 = d2;
}

#endif /* RLK_NOISE_H */
