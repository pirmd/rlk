#include <assert.h>
#include <stdint.h>
#include "rng.h"

/* rlk_rng_seed tests ---------------------------------------------------------- */

static void
test_rlk_rng_seed_basic(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 12345ULL);
    assert(rng.state == 12345ULL);
}

static void
test_rlk_rng_seed_zero(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 0ULL);
    /* splitmix64 handles 0 as valid seed */
    assert(rng.state == 0ULL);
}

static void
test_rlk_rng_seed_max(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, UINT64_MAX);
    assert(rng.state == UINT64_MAX);
}

/* rlk_rng_next_u64 tests ------------------------------------------------------ */

static void
test_rlk_rng_next_u64_non_null(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 42ULL);
    uint64_t v = rlk_rng_next_u64(&rng);
    /* splitmix64 never returns 0 for any state */
    assert(v != 0);
}

static void
test_rlk_rng_next_u64_deterministic(void) {
    rlk_rng_t rng1, rng2;
    rlk_rng_seed(&rng1, 999ULL);
    rlk_rng_seed(&rng2, 999ULL);
    
    uint64_t v1 = rlk_rng_next_u64(&rng1);
    uint64_t v2 = rlk_rng_next_u64(&rng2);
    assert(v1 == v2);
    
    v1 = rlk_rng_next_u64(&rng1);
    v2 = rlk_rng_next_u64(&rng2);
    assert(v1 == v2);
}

static void
test_rlk_rng_next_u64_sequence(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 1ULL);
    
    uint64_t v1 = rlk_rng_next_u64(&rng);
    uint64_t v2 = rlk_rng_next_u64(&rng);
    assert(v1 != v2); /* Should produce different values */
}

/* rlk_rng_next_u32 tests ------------------------------------------------------ */

static void
test_rlk_rng_next_u32_deterministic(void) {
    rlk_rng_t rng1, rng2;
    rlk_rng_seed(&rng1, 777ULL);
    rlk_rng_seed(&rng2, 777ULL);
    
    uint32_t v1 = rlk_rng_next_u32(&rng1);
    uint32_t v2 = rlk_rng_next_u32(&rng2);
    assert(v1 == v2);
}

/* rlk_rng_next_float tests ----------------------------------------------------- */

static void
test_rlk_rng_next_float_range(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 123ULL);
    
    for (int i = 0; i < 1000; i++) {
        float v = rlk_rng_next_float(&rng);
        assert(v >= 0.0f && v < 1.0f);
    }
}

static void
test_rlk_rng_next_float_deterministic(void) {
    rlk_rng_t rng1, rng2;
    rlk_rng_seed(&rng1, 456ULL);
    rlk_rng_seed(&rng2, 456ULL);
    
    float v1 = rlk_rng_next_float(&rng1);
    float v2 = rlk_rng_next_float(&rng2);
    assert(v1 == v2);
}

/* rlk_rng_next_range tests ----------------------------------------------------- */

static void
test_rlk_rng_next_range_single_value(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 1ULL);
    
    int v = rlk_rng_next_range(&rng, 5, 5);
    assert(v == 5);
}

static void
test_rlk_rng_next_range_bounds(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 1ULL);
    
    for (int i = 0; i < 1000; i++) {
        int v = rlk_rng_next_range(&rng, 0, 10);
        assert(v >= 0 && v <= 10);
    }
}

static void
test_rlk_rng_next_range_negative(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 1ULL);
    
    for (int i = 0; i < 1000; i++) {
        int v = rlk_rng_next_range(&rng, -5, 5);
        assert(v >= -5 && v <= 5);
    }
}

static void
test_rlk_rng_next_range_deterministic(void) {
    rlk_rng_t rng1, rng2;
    rlk_rng_seed(&rng1, 111ULL);
    rlk_rng_seed(&rng2, 111ULL);
    
    int v1 = rlk_rng_next_range(&rng1, 10, 20);
    int v2 = rlk_rng_next_range(&rng2, 10, 20);
    assert(v1 == v2);
}

static void
test_rlk_rng_next_range_uniform_distribution(void) {
    /* Test that all values in range are possible */
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 7777ULL);
    
    /* Use a simple range and check we can get both values */
    int counts[2] = {0};
    for (int i = 0; i < 1000; i++) {
        int v = rlk_rng_next_range(&rng, 0, 1);
        assert(v == 0 || v == 1);
        counts[v]++;
    }
    /* Both values should appear (with rejection sampling, bias is eliminated) */
    assert(counts[0] > 0);
    assert(counts[1] > 0);
}

/* rlk_rng_next_bool tests ----------------------------------------------------- */

static void
test_rlk_rng_next_bool_range(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 42ULL);
    
    for (int i = 0; i < 1000; i++) {
        int v = rlk_rng_next_bool(&rng);
        assert(v == 0 || v == 1);
    }
}

static void
test_rlk_rng_next_bool_deterministic(void) {
    rlk_rng_t rng1, rng2;
    rlk_rng_seed(&rng1, 888ULL);
    rlk_rng_seed(&rng2, 888ULL);
    
    int v1 = rlk_rng_next_bool(&rng1);
    int v2 = rlk_rng_next_bool(&rng2);
    assert(v1 == v2);
}

static void
test_rlk_rng_next_bool_distribution(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 9999ULL);
    
    int counts[2] = {0};
    for (int i = 0; i < 1000; i++) {
        counts[rlk_rng_next_bool(&rng)]++;
    }
    /* Both values should appear */
    assert(counts[0] > 0);
    assert(counts[1] > 0);
}

/* Main test runner ------------------------------------------------------------ */

int
main(void) {
    /* rlk_rng_seed tests */
    test_rlk_rng_seed_basic();
    test_rlk_rng_seed_zero();
    test_rlk_rng_seed_max();
    
    /* rlk_rng_next_u64 tests */
    test_rlk_rng_next_u64_non_null();
    test_rlk_rng_next_u64_deterministic();
    test_rlk_rng_next_u64_sequence();
    
    /* rlk_rng_next_u32 tests */
    test_rlk_rng_next_u32_deterministic();
    
    /* rlk_rng_next_float tests */
    test_rlk_rng_next_float_range();
    test_rlk_rng_next_float_deterministic();
    
    /* rlk_rng_next_range tests */
    test_rlk_rng_next_range_single_value();
    test_rlk_rng_next_range_bounds();
    test_rlk_rng_next_range_negative();
    test_rlk_rng_next_range_deterministic();
    test_rlk_rng_next_range_uniform_distribution();
    
    /* rlk_rng_next_bool tests */
    test_rlk_rng_next_bool_range();
    test_rlk_rng_next_bool_deterministic();
    test_rlk_rng_next_bool_distribution();
    
    return 0;
}
