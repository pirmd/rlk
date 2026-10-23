#ifndef RLK_RNG_H
#define RLK_RNG_H

#include <assert.h>
#include <stdint.h>

/*
 * Deterministic, allocation-free PRNG based on splitmix64 (public domain).
 *
 * SplitMix64: period 2^64, passes BigCrush for lower 30 bits.
 * For higher quality requirements, consider pcg64 or xoshiro256.
 */
 

typedef struct {
    uint64_t state;
} rlk_rng_t;

/* Initialize a stream from a 64-bit seed. */
static inline void
rlk_rng_seed(rlk_rng_t *rng, uint64_t seed) {
    assert(rng);
    rng->state = seed;
}

/* Next 64 raw bits of the stream. */
static inline uint64_t
rlk_rng_next_u64(rlk_rng_t *rng) {
    assert(rng);
    uint64_t z = (rng->state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    return z;
}

/* Next 32 raw bits of the stream. */
static inline uint32_t
rlk_rng_next_u32(rlk_rng_t *rng) {
    return (uint32_t)rlk_rng_next_u64(rng);
}

/* Uniform float in [0, 1). Scaled from the full 64-bit draw (no modulo
 * bias); the float result carries ~24-bit resolution.
 */
static inline float
rlk_rng_next_float(rlk_rng_t *rng) {
    return (float)(rlk_rng_next_u64(rng) >> 11) * (1.0f / (1ULL << 53));
}

/* Uniform int in [lo, hi] inclusive.
 * Uses rejection sampling to avoid modulo bias.
 */
static inline int
rlk_rng_next_range(rlk_rng_t *rng, int lo, int hi) {
    assert(lo <= hi);
    uint64_t span = (uint64_t)hi - (uint64_t)lo;
    assert(span <= UINT32_MAX);

    if (span == 0) return lo;
    uint64_t threshold = UINT64_MAX - (UINT64_MAX % (span + 1));
    uint64_t v;
    do {
        v = rlk_rng_next_u64(rng);
    } while (v >= threshold);
    return lo + (int)(v % (span + 1));
}

/* Next boolean value. Returns 0 or 1. */
static inline int
rlk_rng_next_bool(rlk_rng_t *rng) {
    return (int)(rlk_rng_next_u64(rng) & 1);
}

#endif /* RLK_RNG_H */
