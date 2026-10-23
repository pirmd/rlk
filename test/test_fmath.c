#include <assert.h>
#include "fmath.h"

/* rlk_clampf tests ------------------------------------------------------------ */

static void
test_rlk_clampf_within_range(void) {
	assert(rlk_clampf(5.0f, 0.0f, 10.0f) == 5.0f);
	assert(rlk_clampf(0.0f, 0.0f, 10.0f) == 0.0f);
	assert(rlk_clampf(10.0f, 0.0f, 10.0f) == 10.0f);
}

static void
test_rlk_clampf_below_range(void) {
	assert(rlk_clampf(-5.0f, 0.0f, 10.0f) == 0.0f);
	assert(rlk_clampf(-100.0f, 0.0f, 10.0f) == 0.0f);
}

static void
test_rlk_clampf_above_range(void) {
	assert(rlk_clampf(15.0f, 0.0f, 10.0f) == 10.0f);
	assert(rlk_clampf(1000.0f, 0.0f, 10.0f) == 10.0f);
}

static void
test_rlk_clampf_negative_range(void) {
	assert(rlk_clampf(-5.0f, -10.0f, -1.0f) == -5.0f);
	assert(rlk_clampf(-15.0f, -10.0f, -1.0f) == -10.0f);
	assert(rlk_clampf(5.0f, -10.0f, -1.0f) == -1.0f);
}

/* rlk_lerp tests --------------------------------------------------------------- */

static void
test_rlk_lerp_basic(void) {
	assert(rlk_lerp(0.0f, 10.0f, 0.0f) == 0.0f);
	assert(rlk_lerp(0.0f, 10.0f, 1.0f) == 10.0f);
	assert(rlk_lerp(0.0f, 10.0f, 0.5f) == 5.0f);
	assert(rlk_lerp(1.0f, 3.0f, 0.5f) == 2.0f);
}

static void
test_rlk_lerp_negative(void) {
	assert(rlk_lerp(-10.0f, 10.0f, 0.5f) == 0.0f);
	assert(rlk_lerp(10.0f, -10.0f, 0.5f) == 0.0f);
}

static void
test_rlk_lerp_extrapolation(void) {
	/* t outside [0,1] is allowed (unclamped) */
	assert(rlk_lerp(0.0f, 10.0f, -1.0f) == -10.0f);
	assert(rlk_lerp(0.0f, 10.0f, 2.0f) == 20.0f);
}

/* rlk_smoothstep tests --------------------------------------------------------- */

static void
test_rlk_smoothstep_boundaries(void) {
	assert(rlk_smoothstep(0.0f) == 0.0f);
	assert(rlk_smoothstep(1.0f) == 1.0f);
}

static void
test_rlk_smoothstep_midpoint(void) {
	float mid = rlk_smoothstep(0.5f);
	/* smoothstep(0.5) = 0.5 * 0.5 * (3 - 2*0.5) = 0.25 * 2 = 0.5 */
	assert(mid == 0.5f);
}

static void
test_rlk_smoothstep_quadratic(void) {
	/* smoothstep is cubic: t^2 * (3 - 2t) */
	float v = rlk_smoothstep(0.25f);
	/* 0.25^2 * (3 - 0.5) = 0.0625 * 2.5 = 0.15625 */
	assert(v > 0.15f && v < 0.16f);
}

/* rlk_dist2f tests ------------------------------------------------------------ */

static void
test_rlk_dist2f_origin(void) {
	assert(rlk_dist2f(0.0f, 0.0f) == 0.0f);
}

static void
test_rlk_dist2f_axes(void) {
	assert(rlk_dist2f(3.0f, 0.0f) > 2.9999f && rlk_dist2f(3.0f, 0.0f) < 3.0001f);
	assert(rlk_dist2f(0.0f, 4.0f) > 3.9999f && rlk_dist2f(0.0f, 4.0f) < 4.0001f);
}

static void
test_rlk_dist2f_diagonal(void) {
	/* sqrt(3^2 + 4^2) = 5 */
	assert(rlk_dist2f(3.0f, 4.0f) > 4.9999f && rlk_dist2f(3.0f, 4.0f) < 5.0001f);
}

/* Main ----------------------------------------------------------------------- */

int
main(void) {
	test_rlk_clampf_within_range();
	test_rlk_clampf_below_range();
	test_rlk_clampf_above_range();
	test_rlk_clampf_negative_range();

	test_rlk_lerp_basic();
	test_rlk_lerp_negative();
	test_rlk_lerp_extrapolation();

	test_rlk_smoothstep_boundaries();
	test_rlk_smoothstep_midpoint();
	test_rlk_smoothstep_quadratic();

	test_rlk_dist2f_origin();
	test_rlk_dist2f_axes();
	test_rlk_dist2f_diagonal();

	return 0;
}
