#include <assert.h>
#include <float.h>
#include "noise.h"

/* Fixture -------------------------------------------------------------------- */

static rlk_noise_t g_noise;

static void
fixture_setup(void) {
	g_noise.seed = 12345;
}

static void
fixture_teardown(void) {
	(void)g_noise;
}

/* rlk_noise_point_hash tests -------------------------------------------------- */

static void
test_rlk_noise_point_hash_basic(void) {
	fixture_setup();
	float v = rlk_noise_point_hash(&g_noise, 0, 0);
	assert(v >= 0.0f && v < 1.0f);
	fixture_teardown();
}

static void
test_rlk_noise_point_hash_deterministic(void) {
	fixture_setup();
	float v1 = rlk_noise_point_hash(&g_noise, 10, 20);
	float v2 = rlk_noise_point_hash(&g_noise, 10, 20);
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

/* Weighted average of [0,1) layers stays in [0,1). */
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

/* rlk_noise_cellular tests -------------------------------------------------------- */

static void
test_rlk_noise_cellular_basic(void) {
	fixture_setup();
	rlk_noise_cellular_t c;
	rlk_noise_cellular(&g_noise, 3.7f, -2.1f, 16.0f, &c);
	assert(c.d1 >= 0.0f);
	assert(c.d1 <= c.d2);
	assert(c.d2 < FLT_MAX); /* the 5x5 scan always finds two points */
	fixture_teardown();
}

static void
test_rlk_noise_cellular_deterministic(void) {
	fixture_setup();
	rlk_noise_cellular_t a, b;
	rlk_noise_cellular(&g_noise, 10.3f, 20.8f, 8.0f, &a);
	rlk_noise_cellular(&g_noise, 10.3f, 20.8f, 8.0f, &b);
	assert(a.id == b.id && a.cx == b.cx && a.cy == b.cy);
	assert(a.d1 == b.d1 && a.d2 == b.d2);
	fixture_teardown();
}

static void
test_rlk_noise_cellular_seed_changes_result(void) {
	rlk_noise_t n2 = { .seed = 999 };
	fixture_setup();
	rlk_noise_cellular_t a, b;
	rlk_noise_cellular(&g_noise, 10.3f, 20.8f, 8.0f, &a);
	rlk_noise_cellular(&n2, 10.3f, 20.8f, 8.0f, &b);
	assert(a.id != b.id || a.d1 != b.d1);
	fixture_teardown();
}

/* Invariants over a grid of sample points: id matches the returned cell,
   the cell lies in the 5x5 neighborhood of the containing cell, and F1 is
   bounded by the farthest feature point of the containing cell. */
static void
test_rlk_noise_cellular_invariants(void) {
	fixture_setup();
	const float cell_size = 16.0f;
	for (int k = 0; k < 50; k++) {
		const float x = -100.0f + 4.13f * (float)k;
		const float y = 55.0f - 3.71f * (float)k;
		rlk_noise_cellular_t c;
		rlk_noise_cellular(&g_noise, x, y, cell_size, &c);
		const int bx = (int)floorf(x / cell_size);
		const int by = (int)floorf(y / cell_size);
		assert(c.id == rlk_hash_combine2(g_noise.seed, c.cx, c.cy));
		assert(c.cx - bx >= -2 && c.cx - bx <= 2);
		assert(c.cy - by >= -2 && c.cy - by <= 2);
		assert(c.d1 <= 1.21f * cell_size); /* sqrt(2) * 0.85 * cell, jitter bound */
		assert(c.d1 <= c.d2);
	}
	fixture_teardown();
}

/* Main ----------------------------------------------------------------------- */

int
main(void) {
	/* rlk_noise_point_hash tests */
	test_rlk_noise_point_hash_basic();
	test_rlk_noise_point_hash_deterministic();

	/* rlk_noise_value tests */
	test_rlk_noise_value_basic();
	test_rlk_noise_value_deterministic();
	test_rlk_noise_value_smooth();

	/* rlk_noise_fbm tests */
	test_rlk_noise_fbm_basic();
	test_rlk_noise_fbm_deterministic();
	test_rlk_noise_fbm_range();

	/* rlk_noise_cellular tests */
	test_rlk_noise_cellular_basic();
	test_rlk_noise_cellular_deterministic();
	test_rlk_noise_cellular_seed_changes_result();
	test_rlk_noise_cellular_invariants();

	return 0;
}
