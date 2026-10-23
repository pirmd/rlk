#ifndef RLK_HASH_H
#define RLK_HASH_H

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

#endif /* RLK_HASH_H */
