#include <assert.h>
#include "noise.h"

/* Fixture -------------------------------------------------------------------- */

static rlk_noise_t g_noise;

static void
fixture_setup(void) {
	rlk_noise_seed(&g_noise, 12345);
}

static void
fixture_teardown(void) {
	(void)g_noise;
}

/* rlk_noise_seed tests -------------------------------------------------------- */

static void
test_rlk_noise_seed_basic(void) {
	rlk_noise_t n;
	uint64_t seed = 42;
	rlk_noise_seed(&n, seed);
	assert(n.seed == seed);
}

static void
test_rlk_noise_seed_different_seeds(void) {
	rlk_noise_t n1, n2;
	rlk_noise_seed(&n1, 1);
	rlk_noise_seed(&n2, 2);
	assert(n1.seed != n2.seed);
}

/* rlk_noise_lattice tests ---------------------------------------------------- */

static void
test_rlk_noise_lattice_basic(void) {
	fixture_setup();
	float v = rlk_noise_lattice(&g_noise, 0, 0);
	assert(v >= 0.0f && v < 1.0f);
	fixture_teardown();
}

static void
test_rlk_noise_lattice_deterministic(void) {
	fixture_setup();
	float v1 = rlk_noise_lattice(&g_noise, 10, 20);
	float v2 = rlk_noise_lattice(&g_noise, 10, 20);
	assert(v1 == v2);
	fixture_teardown();
}

/* rlk_noise_value tests ---------------------------------------------------- */

static void
test_rlk_noise_value_basic(void) {
	fixture_setup();
	float v = rlk_noise_value(&g_noise, 0.0f, 0.0f);
	assert(v >= 0.0f && v < 1.0f);
	fixture_teardown();
}

static void
test_rlk_noise_value_deterministic(void) {
	fixture_setup();
	float v1 = rlk_noise_value(&g_noise, 1.5f, 2.5f);
	float v2 = rlk_noise_value(&g_noise, 1.5f, 2.5f);
	assert(v1 == v2);
	fixture_teardown();
}

static void
test_rlk_noise_value_smooth(void) {
	fixture_setup();
	/* Value noise should be smooth: nearby points should have similar values */
	float v1 = rlk_noise_value(&g_noise, 1.0f, 1.0f);
	float v2 = rlk_noise_value(&g_noise, 1.01f, 1.01f);
	float diff = (v1 > v2) ? (v1 - v2) : (v2 - v1);
	assert(diff < 0.5f); /* Smooth interpolation should limit the difference */
	fixture_teardown();
}

/* rlk_noise_perlin tests ------------------------------------------------------ */

static void
test_rlk_noise_perlin_basic(void) {
	fixture_setup();
	float v = rlk_noise_perlin(&g_noise, 0.0f, 0.0f);
	assert(v >= -1.0f && v <= 1.0f);
	fixture_teardown();
}

static void
test_rlk_noise_perlin_deterministic(void) {
	fixture_setup();
	float v1 = rlk_noise_perlin(&g_noise, 2.5f, 3.5f);
	float v2 = rlk_noise_perlin(&g_noise, 2.5f, 3.5f);
	assert(v1 == v2);
	fixture_teardown();
}

static void
test_rlk_noise_perlin_range(void) {
	fixture_setup();
	/* Perlin noise should span roughly [-1, 1] */
	float min_v = 1.0f, max_v = -1.0f;
	for (int x = 0; x < 100; x++) {
		for (int y = 0; y < 100; y++) {
			float v = rlk_noise_perlin(&g_noise, (float)x * 0.1f, (float)y * 0.1f);
			if (v < min_v) min_v = v;
			if (v > max_v) max_v = v;
		}
	}
	assert(min_v < 0.0f && max_v > 0.0f); /* Should span both negative and positive */
	fixture_teardown();
}

/* fade: 6t^5 - 15t^4 + 10t^3, symmetric around 0.5, monotone on [0,1]. */
static void
test_rlk_noise_perlin_fade(void) {
	assert(rlk_noise_perlin_fade(0.0f) == 0.0f);
	assert(rlk_noise_perlin_fade(1.0f) == 1.0f);
	assert(rlk_noise_perlin_fade(0.5f) == 0.5f);
	assert(rlk_noise_perlin_fade(0.25f) - rlk_noise_perlin_fade(0.75f) < 1e-6f);
	for (int i = 0; i < 10; i++) {
		float t = (float)i / 10.0f;
		assert(rlk_noise_perlin_fade(t) <= rlk_noise_perlin_fade(t + 0.1f));
	}
}

/* grad: dot product with one of the 8 unit/diagonal gradient vectors. */
static void
test_rlk_noise_perlin_grad(void) {
	/* hash 0 -> g 0 -> (1,1) */
	assert(rlk_noise_perlin_grad(0, 2.0f, 3.0f) == 5.0f);
	/* hash 4 -> g 4 -> (1,0) */
	assert(rlk_noise_perlin_grad(4, 2.0f, 3.0f) == 2.0f);
	/* hash 7 -> g 7 -> (0,-1) */
	assert(rlk_noise_perlin_grad(7, 2.0f, 3.0f) == -3.0f);
	/* Orthogonal input gives zero dot product. */
	assert(rlk_noise_perlin_grad(4, 0.0f, 3.0f) == 0.0f);
}

/* rlk_noise_fbm tests --------------------------------------------------------- */

static void
test_rlk_noise_fbm_basic(void) {
	fixture_setup();
	float v = rlk_noise_fbm(&g_noise, 0.0f, 0.0f, 1, 1.0f, 0.5f);
	assert(v >= 0.0f && v <= 1.0f);
	fixture_teardown();
}

static void
test_rlk_noise_fbm_deterministic(void) {
	fixture_setup();
	float v1 = rlk_noise_fbm(&g_noise, 5.0f, 5.0f, 3, 0.1f, 0.5f);
	float v2 = rlk_noise_fbm(&g_noise, 5.0f, 5.0f, 3, 0.1f, 0.5f);
	assert(v1 == v2);
	fixture_teardown();
}

/* Weighted average of [0,1) layers stays in [0,1]. */
static void
test_rlk_noise_fbm_range(void) {
	fixture_setup();
	for (int x = 0; x < 100; x++) {
		for (int y = 0; y < 100; y++) {
			float v = rlk_noise_fbm(&g_noise, (float)x * 0.37f, (float)y * 0.37f,
			                        4, 0.1f, 0.5f);
			assert(v >= 0.0f && v <= 1.0f);
		}
	}
	fixture_teardown();
}

/* Main ----------------------------------------------------------------------- */

int
main(void) {
	/* rlk_noise_seed tests */
	test_rlk_noise_seed_basic();
	test_rlk_noise_seed_different_seeds();

	/* rlk_noise_lattice tests */
	test_rlk_noise_lattice_basic();
	test_rlk_noise_lattice_deterministic();

	/* rlk_noise_value tests */
	test_rlk_noise_value_basic();
	test_rlk_noise_value_deterministic();
	test_rlk_noise_value_smooth();

	/* rlk_noise_perlin tests */
	test_rlk_noise_perlin_basic();
	test_rlk_noise_perlin_deterministic();
	test_rlk_noise_perlin_range();
	test_rlk_noise_perlin_fade();
	test_rlk_noise_perlin_grad();

	/* rlk_noise_fbm tests */
	test_rlk_noise_fbm_basic();
	test_rlk_noise_fbm_deterministic();
	test_rlk_noise_fbm_range();

	return 0;
}
