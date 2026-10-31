#ifndef RLK_HASH_H
#define RLK_HASH_H

/* Deterministic integer hashing: spatial hash at a position (hash_at),
 * avalanche-mixed sub-seeds (hash_sub). */

#include <stdint.h>

/* Combine a seed with two 32-bit coordinates to produce a unique hash.
 * Used for spatial hashing (e.g., noise, worldgen).
 * NOTE: output is not fully avalanched; pipe through rlk_hash_sub()
 * before extracting low bits (e.g., for float conversion).
 */
static inline uint64_t
rlk_hash_combine2(uint64_t seed, int a, int b) {
    return seed
         ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(uint32_t)a)
         ^ (0x6A09E667F3BCC909ULL * (uint64_t)(uint32_t)b);
}

/* Derive a fully-mixed sub-seed from a base seed and a salt.
 * Use to create independent streams from a single seed.
 */
static inline uint64_t
rlk_hash_sub(uint64_t base, uint64_t salt) {
    uint64_t z = base ^ (salt * 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Avalanched spatial hash at integer position (x, y): the single
 * "hash at position" primitive of the lib (wg/ca/bsp rand, noise lattice,
 * POI seeds are all built on it). salt decorrelates streams; salt 0 is the
 * plain avalanche of rlk_hash_combine2. */
static inline uint64_t
rlk_hash_at(uint64_t seed, int x, int y, uint64_t salt) {
    return rlk_hash_sub(rlk_hash_combine2(seed, x, y), salt);
}

/* Normalize a hash to a float in [0,1) (top 24 bits). */
static inline float
rlk_hash_normf(uint64_t h) {
    return (float)(h >> 40) * (1.0f / 16777216.0f);
}

#endif /* RLK_HASH_H */
