#include "test.h"
#include "rl/rng.h"

/* splitmix64 is a known-answer reference: seed 0 must produce a fixed stream. */
static void
test_seed_zero_first_value(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 0ULL);
    /* Reference values for splitmix64, seed 0. */
    ASSERT(rlk_rng_next_u64(&rng) == 0xE220A8397B1DCDAFULL);
    ASSERT(rlk_rng_next_u64(&rng) == 0x6E789E6AA1B965F4ULL);
    ASSERT(rlk_rng_next_u64(&rng) == 0x06C45D188009454FULL);
}

static void
test_seed_one_known_value(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 1ULL);
    ASSERT(rlk_rng_next_u64(&rng) == 0x910A2DEC89025CC1ULL);
    ASSERT(rlk_rng_next_u64(&rng) == 0xBEEB8DA1658EEC67ULL);
    ASSERT(rlk_rng_next_u64(&rng) == 0xF893A2EEFB32555EULL);
}

/* Same seed => identical stream; different seeds => different first value. */
static void
test_reproducibility(void) {
    rlk_rng_t a, b;
    rlk_rng_seed(&a, 0xDEADBEEF12345678ULL);
    rlk_rng_seed(&b, 0xDEADBEEF12345678ULL);
    for (int i = 0; i < 100; i++) {
        ASSERT(rlk_rng_next_u64(&a) == rlk_rng_next_u64(&b));
    }
    rlk_rng_t c;
    rlk_rng_seed(&c, 0xC0FFEEULL);
    ASSERT(rlk_rng_next_u64(&a) != rlk_rng_next_u64(&c));
}

static void
test_range_in_bounds(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 42ULL);
    for (int i = 0; i < 10000; i++) {
        int32_t v = rlk_rng_next_range(&rng, -5, 5);
        ASSERT(v >= -5 && v <= 5);
    }
    /* Single-value range. */
    ASSERT(rlk_rng_next_range(&rng, 7, 7) == 7);
}

static void
test_float_in_unit(void) {
    rlk_rng_t rng;
    rlk_rng_seed(&rng, 7ULL);
    for (int i = 0; i < 10000; i++) {
        float f = rlk_rng_next_float(&rng);
        ASSERT(f >= 0.0f && f < 1.0f);
    }
}

int
main(void) {
    test_seed_zero_first_value();
    test_seed_one_known_value();
    test_reproducibility();
    test_range_in_bounds();
    test_float_in_unit();
    return 0;
}
