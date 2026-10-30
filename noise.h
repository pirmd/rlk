#ifndef RLK_NOISE_H
#define RLK_NOISE_H

#include <assert.h>
#include <float.h>
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

static inline void
rlk_noise_seed(rlk_noise_t *n, uint64_t seed) {
    assert(n);
    n->seed = seed;
}

/* Hash lattice point (ix,iy) with seed into [0,1). */
static inline float
rlk_noise_lattice(const rlk_noise_t *n, int ix, int iy) {
    assert(n);
    uint64_t z = rlk_hash_combine2(n->seed, ix, iy);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (float)(z >> 40) * (1.0f / 16777216.0f);
}

/* 2D gradient vectors for Perlin noise (8 directions) */
static const float RLK_NOISE_PERLIN_GRADIENTS[8][2] = {
    { 1.0f,  1.0f}, {-1.0f,  1.0f}, { 1.0f, -1.0f}, {-1.0f, -1.0f},
    { 1.0f,  0.0f}, {-1.0f,  0.0f}, { 0.0f,  1.0f}, { 0.0f, -1.0f}
};

/* Perlin noise fade function: 6t^5 - 15t^4 + 10t^3 */
static inline float
rlk_noise_perlin_fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/* Perlin noise gradient dot product */
static inline float
rlk_noise_perlin_grad(uint64_t hash, float x, float y) {
    int g = (int)(hash & 7);
    return x * RLK_NOISE_PERLIN_GRADIENTS[g][0] +
           y * RLK_NOISE_PERLIN_GRADIENTS[g][1];
}

/* Bilinear Perlin noise at (x,y) in [-1,1]. */
static inline float
rlk_noise_perlin(const rlk_noise_t *n, float x, float y) {
    assert(n);
    assert(x == x && y == y);
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    float fx = x - (float)x0;
    float fy = y - (float)y0;

    float u = rlk_noise_perlin_fade(fx);
    float v = rlk_noise_perlin_fade(fy);

    uint64_t h00 = rlk_hash_combine2(n->seed, x0,     y0);
    uint64_t h10 = rlk_hash_combine2(n->seed, x0 + 1, y0);
    uint64_t h01 = rlk_hash_combine2(n->seed, x0,     y0 + 1);
    uint64_t h11 = rlk_hash_combine2(n->seed, x0 + 1, y0 + 1);

    float g00 = rlk_noise_perlin_grad(h00, fx,     fy);
    float g10 = rlk_noise_perlin_grad(h10, fx - 1, fy);
    float g01 = rlk_noise_perlin_grad(h01, fx,     fy - 1);
    float g11 = rlk_noise_perlin_grad(h11, fx - 1, fy - 1);

    float a = rlk_lerp(g00, g10, u);
    float b = rlk_lerp(g01, g11, u);
    return rlk_lerp(a, b, v);
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

    float v00 = rlk_noise_lattice(n, x0,     y0);
    float v10 = rlk_noise_lattice(n, x0 + 1, y0);
    float v01 = rlk_noise_lattice(n, x0,     y0 + 1);
    float v11 = rlk_noise_lattice(n, x0 + 1, y0 + 1);

    float sx = rlk_smoothstep(fx);
    float sy = rlk_smoothstep(fy);
    float a = rlk_lerp(v00, v10, sx);
    float b = rlk_lerp(v01, v11, sx);
    return rlk_lerp(a, b, sy);
}

/* Fractal Brownian Motion. Sum of `octaves` noise layers. Output in ~[0,1]. */
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
   side `cell`, one point jittered inside each cell. Pure function of
   (seed, x, y): deterministic, no allocation. */
typedef struct {
    uint64_t id;   /* identity (hash) of the closest feature point */
    int cx, cy;    /* cell of the closest feature point */
    float d1;      /* distance to the closest feature point (F1) */
    float d2;      /* distance to the second closest feature point (F2) */
} rlk_noise_cell_t;

/* Closest feature points at (x, y): scans the 5x5 cells around the
   containing cell, which provably holds F1/F2 for jitter in [0.15, 0.85]. */
static inline void
rlk_noise_cell(const rlk_noise_t *n, float x, float y, float cell,
               rlk_noise_cell_t *out) {
    assert(n);
    assert(out);
    assert(cell > 0.0f);
    assert(x == x && y == y);
    const int bx = (int)floorf(x / cell);
    const int by = (int)floorf(y / cell);
    float d1 = FLT_MAX, d2 = FLT_MAX;
    uint64_t id = 0;
    int cx = bx, cy = by;
    for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
            const int ccx = bx + i;
            const int ccy = by + j;
            const uint64_t h = rlk_hash_combine2(n->seed, ccx, ccy);
            const float fx = 0.15f + 0.70f * (float)(rlk_hash_sub(h, 1) >> 40) * (1.0f / 16777216.0f);
            const float fy = 0.15f + 0.70f * (float)(rlk_hash_sub(h, 2) >> 40) * (1.0f / 16777216.0f);
            const float qx = ((float)ccx + fx) * cell - x;
            const float qy = ((float)ccy + fy) * cell - y;
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
