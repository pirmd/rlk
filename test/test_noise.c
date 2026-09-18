#include "test.h"
#include "rl/noise.h"

/* Lattice values are in [0, 1). */
static void
test_lattice_bounds(void) {
    rlk_noise_t n;
    rlk_noise_seed(&n, 123ULL);
    for (int y = -20; y < 20; y++) {
        for (int x = -20; x < 20; x++) {
            float v = rlk_noise_lattice(&n, x, y);
            ASSERT(v >= 0.0f && v < 1.0f);
        }
    }
}

/* Lattice is deterministic: same (seed, ix, iy) => same value. */
static void
test_lattice_deterministic(void) {
    rlk_noise_t n;
    rlk_noise_seed(&n, 99ULL);
    float a = rlk_noise_lattice(&n, 5, 7);
    float b = rlk_noise_lattice(&n, 5, 7);
    ASSERT(a == b);
}

/* Value noise is bounded and interpolates between lattice values. */
static void
test_value_bounds_and_continuity(void) {
    rlk_noise_t n;
    rlk_noise_seed(&n, 42ULL);
    for (float y = -5.0f; y < 5.0f; y += 0.25f) {
        for (float x = -5.0f; x < 5.0f; x += 0.25f) {
            float v = rlk_noise_value(&n, x, y);
            ASSERT(v >= 0.0f && v <= 1.0f);
        }
    }
}

/* Value noise at lattice corners equals the lattice values. */
static void
test_value_at_lattice(void) {
    rlk_noise_t n;
    rlk_noise_seed(&n, 7ULL);
    ASSERT(rlk_noise_value(&n, 3.0f, 4.0f) == rlk_noise_lattice(&n, 3, 4));
}

/* fbm is bounded in [0, 1] for the default normalized sum. */
static void
test_fbm_bounds(void) {
    rlk_noise_t n;
    rlk_noise_seed(&n, 31337ULL);
    for (float y = 0.0f; y < 10.0f; y += 0.5f) {
        for (float x = 0.0f; x < 10.0f; x += 0.5f) {
            float v = rlk_noise_fbm(&n, x, y, 4, 0.1f, 0.5f);
            ASSERT(v >= 0.0f && v <= 1.0f);
        }
    }
}

static void
test_clumping_bounds(void) {
    rlk_noise_t n;
    rlk_noise_seed(&n, 0xBEEFULL);
    for (float y = 0.0f; y < 64.0f; y += 4.0f) {
        for (float x = 0.0f; x < 64.0f; x += 4.0f) {
            float v = rlk_noise_clumping(&n, x, y, 0xC0FFEEULL);
            ASSERT(v >= 0.0f && v <= 1.0f);
        }
    }
}

int
main(void) {
    test_lattice_bounds();
    test_lattice_deterministic();
    test_value_bounds_and_continuity();
    test_value_at_lattice();
    test_fbm_bounds();
    test_clumping_bounds();
    return 0;
}
