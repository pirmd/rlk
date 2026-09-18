#ifndef RL_RNG_H
#define RL_RNG_H

#include <assert.h>
#include <stdint.h>

/*
 * Deterministic, allocation-free PRNG.
 *
 * Based on splitmix64 ( Sebastiano Vigna, public domain ). Streams are fully
 * reproducible from a 64-bit seed: the same seed yields the same sequence on
 * any platform with uint64_t arithmetic. No global state; the caller owns the
 * rlk_rng_t. See docs/ALLOCATION.md (no hidden state, POD).
 */
typedef struct {
    uint64_t state;
} rlk_rng_t;

/* Initialize a stream from a 64-bit seed. */
static inline void
rlk_rng_seed(rlk_rng_t *rng, uint64_t seed) {
    assert(rng);
    /* splitmix64 scrambles any input (including 0) into a good state. */
    rng->state = seed;
}

/* Next 64 raw bits of the stream. */
static inline uint64_t
rlk_rng_next_u64(rlk_rng_t *rng) {
    assert(rng);
    uint64_t z = (rng->state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Uniform float in [0, 1). 24-bit mantissa precision. */
static inline float
rlk_rng_next_float(rlk_rng_t *rng) {
    /* 2^24 = 16777216; map [0, 2^24-1] to [0, 1). */
    return (float)(rlk_rng_next_u64(rng) >> 40) * (1.0f / 16777216.0f);
}

/* Uniform int in [lo, hi] inclusive. lo <= hi must hold. */
static inline int32_t
rlk_rng_next_range(rlk_rng_t *rng, int32_t lo, int32_t hi) {
    assert(lo <= hi);
    uint32_t span = (uint32_t)(hi - lo);
    if (span == 0xFFFFFFFFu) {
        return lo + (int32_t)rlk_rng_next_u64(rng);
    }
    return lo + (int32_t)(rlk_rng_next_u64(rng) % ((uint64_t)span + 1));
}

#endif /* RL_RNG_H */
