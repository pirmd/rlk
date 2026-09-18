#ifndef RL_NOISE_H
#define RL_NOISE_H

#include <assert.h>
#include <stdint.h>

/*
 * Deterministic, allocation-free value noise.
 *
 * Produces smooth pseudo-random fields reproducible from a seed. No global
 * state, no allocations; the caller owns rlk_noise_t. The same (seed, x, y)
 * always yields the same value, independently of call order.
 */
typedef struct {
    uint64_t seed;
} rlk_noise_t;

static inline void
rlk_noise_seed(rlk_noise_t *n, uint64_t seed) {
    assert(n);
    n->seed = seed;
}

/*
 * Hash of an integer lattice point (ix, iy) into [0, 1). splitmix64-based,
 * mixes seed and coordinates so nearby lattice points decorrelate.
 */
static inline float
rlk_noise_lattice(const rlk_noise_t *n, int32_t ix, int32_t iy) {
    assert(n);
    uint64_t z = n->seed
               ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(uint32_t)ix)
               ^ (0x6A09E667F3BCC909ULL * (uint64_t)(uint32_t)iy);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (float)(z >> 40) * (1.0f / 16777216.0f);
}

/* Smoothstep interpolation weight for t in [0, 1]. */
static inline float
rlk_noise_smooth(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static inline float
rlk_noise_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/*
 * Bilinearly-interpolated value noise at continuous coordinates (x, y).
 * Output in [0, 1). Reproducible from seed; order-independent.
 */
static inline float
rlk_noise_value(const rlk_noise_t *n, float x, float y) {
    assert(n);
    int32_t x0 = (int32_t)x;
    int32_t y0 = (int32_t)y;
    float fx = x - (float)x0;
    float fy = y - (float)y0;
    if (fx < 0.0f) fx += 1.0f, x0 -= 1;   /* floor for negatives */
    if (fy < 0.0f) fy += 1.0f, y0 -= 1;

    float v00 = rlk_noise_lattice(n, x0,     y0);
    float v10 = rlk_noise_lattice(n, x0 + 1, y0);
    float v01 = rlk_noise_lattice(n, x0,     y0 + 1);
    float v11 = rlk_noise_lattice(n, x0 + 1, y0 + 1);

    float sx = rlk_noise_smooth(fx);
    float sy = rlk_noise_smooth(fy);
    float a = rlk_noise_lerp(v00, v10, sx);
    float b = rlk_noise_lerp(v01, v11, sx);
    return rlk_noise_lerp(a, b, sy);
}

/*
 * Fractal Brownian motion: sum of `octaves` value-noise layers at increasing
 * frequency and decreasing amplitude. Output in approximately [0, 1].
 * `freq` is base frequency; `gain` in (0,1) halves amplitude per octave.
 */
static inline float
rlk_noise_fbm(const rlk_noise_t *n, float x, float y,
              int octaves, float freq, float gain) {
    assert(n);
    assert(octaves > 0);
    assert(gain > 0.0f && gain < 1.0f);
    float amp = 1.0f;
    float sum = 0.0f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; o++) {
        sum  += amp * rlk_noise_value(n, x * freq, y * freq);
        norm += amp;
        amp  *= gain;
        freq *= 2.0f;
    }
    return sum / norm;
}

/*
 * Clumping noise: a single fbm layer used to bias scatter density into
 * clusters (patches of denser vegetation / fewer isolated points). Returns
 * [0, 1] where high values favor placement.
 */
static inline float
rlk_noise_clumping(const rlk_noise_t *n, float x, float y, uint64_t cluster_seed) {
    assert(n);
    rlk_noise_t cn = { .seed = n->seed ^ (cluster_seed * 0x9E3779B97F4A7C15ULL) };
    return rlk_noise_fbm(&cn, x, y, 3, 0.0625f, 0.5f);
}

#endif /* RL_NOISE_H */
