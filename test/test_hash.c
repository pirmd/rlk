#include <assert.h>
#include <stdint.h>
#include "hash.h"

static int
popcount64(uint64_t x) {
    int n = 0;
    while (x) {
        n += (int)(x & 1ULL);
        x >>= 1;
    }
    return n;
}

/* rlk_hash_combine2 tests ------------------------------------------------------- */

static void
test_rlk_hash_combine2_deterministic(void) {
    uint64_t h1 = rlk_hash_combine2(123ULL, 10, 20);
    uint64_t h2 = rlk_hash_combine2(123ULL, 10, 20);
    assert(h1 == h2);
}

static void
test_rlk_hash_combine2_different_coords(void) {
    uint64_t h1 = rlk_hash_combine2(123ULL, 0, 0);
    uint64_t h2 = rlk_hash_combine2(123ULL, 1, 0);
    assert(h1 != h2);
}

static void
test_rlk_hash_combine2_different_seed(void) {
    uint64_t h1 = rlk_hash_combine2(123ULL, 10, 20);
    uint64_t h2 = rlk_hash_combine2(456ULL, 10, 20);
    assert(h1 != h2);
}

/* rlk_hash_sub tests ----------------------------------------------------------- */

static void
test_rlk_hash_sub_mixes(void) {
    /* splitmix64 finalizer should change the value for non-zero input */
    assert(rlk_hash_sub(123ULL, 456ULL) != 123ULL);
    assert(rlk_hash_sub(1ULL, 0ULL) != 1ULL);
}

static void
test_rlk_hash_sub_deterministic(void) {
    uint64_t h1 = rlk_hash_sub(123ULL, 456ULL);
    uint64_t h2 = rlk_hash_sub(123ULL, 456ULL);
    assert(h1 == h2);
}

static void
test_rlk_hash_sub_different_salt(void) {
    uint64_t h1 = rlk_hash_sub(123ULL, 0ULL);
    uint64_t h2 = rlk_hash_sub(123ULL, 1ULL);
    assert(h1 != h2);
}

static void
test_rlk_hash_sub_different_base(void) {
    uint64_t h1 = rlk_hash_sub(123ULL, 456ULL);
    uint64_t h2 = rlk_hash_sub(789ULL, 456ULL);
    assert(h1 != h2);
}

static void
test_rlk_hash_sub_avalanche(void) {
    /* Test that small changes in input produce large changes in output */
    uint64_t h1 = rlk_hash_sub(123ULL, 0ULL);
    uint64_t h2 = rlk_hash_sub(123ULL ^ 1ULL, 0ULL);
    /* Should be significantly different (avalanche effect) */
    uint64_t diff = h1 ^ h2;
    /* At least half the bits should differ */
    assert(popcount64(diff) >= 32);
}

/* Integration tests ----------------------------------------------------------- */

static void
test_rlk_hash_unique_per_coord(void) {
    /* Deterministic: known coords with same seed must give distinct hashes. */
    uint64_t seed = 999ULL;
    uint64_t h0 = rlk_hash_combine2(seed, 0, 0);
    uint64_t h1 = rlk_hash_combine2(seed, 1, 0);
    uint64_t h2 = rlk_hash_combine2(seed, 0, 1);
    uint64_t h3 = rlk_hash_combine2(seed, 1, 1);
    assert(h0 != h1);
    assert(h0 != h2);
    assert(h0 != h3);
    assert(h1 != h2);
    assert(h1 != h3);
    assert(h2 != h3);
}

/* rlk_hash_at tests ------------------------------------------------------------ */

static void
test_rlk_hash_at_equals_combine2_sub(void) {
    /* salt 0: plain avalanche of combine2 */
    for (int x = -3; x <= 3; x++)
        for (int y = -3; y <= 3; y++)
            assert(rlk_hash_at(42ULL, x, y, 0ULL)
                   == rlk_hash_sub(rlk_hash_combine2(42ULL, x, y), 0ULL));
}

static void
test_rlk_hash_at_deterministic_decorrelated(void) {
    assert(rlk_hash_at(7ULL, 3, -5, 0xCAFEULL) == rlk_hash_at(7ULL, 3, -5, 0xCAFEULL));
    assert(rlk_hash_at(7ULL, 3, -5, 0ULL) != rlk_hash_at(7ULL, 3, -5, 1ULL));
    assert(rlk_hash_at(7ULL, 3, -5, 0ULL) != rlk_hash_at(7ULL, 4, -5, 0ULL));
    assert(rlk_hash_at(7ULL, 3, -5, 0ULL) != rlk_hash_at(8ULL, 3, -5, 0ULL));
}

/* rlk_hash_normf tests ---------------------------------------------------------- */

static void
test_rlk_hash_normf_range(void) {
    assert(rlk_hash_normf(0ULL) == 0.0f);
    for (uint64_t h = 0; h < 1000; h++) {
        float f = rlk_hash_normf(h * 0x9E3779B97F4A7C15ULL + 0x123456789ULL);
        assert(f >= 0.0f && f < 1.0f);
    }
    assert(rlk_hash_normf(0xFFFFFFFFFFFFFFFFULL) < 1.0f);
}

/* Main test runner ------------------------------------------------------------ */

int
main(void) {
    /* rlk_hash_combine2 tests */
    test_rlk_hash_combine2_deterministic();
    test_rlk_hash_combine2_different_coords();
    test_rlk_hash_combine2_different_seed();

    /* rlk_hash_sub tests */
    test_rlk_hash_sub_mixes();
    test_rlk_hash_sub_deterministic();
    test_rlk_hash_sub_different_salt();
    test_rlk_hash_sub_different_base();
    test_rlk_hash_sub_avalanche();
    
    /* rlk_hash_at / rlk_hash_normf tests */
    test_rlk_hash_at_equals_combine2_sub();
    test_rlk_hash_at_deterministic_decorrelated();
    test_rlk_hash_normf_range();

    /* Integration tests */
    test_rlk_hash_unique_per_coord();

    return 0;
}
