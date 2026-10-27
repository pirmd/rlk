#include <assert.h>
#include <string.h>
#include "tilemap.h"

/* Fixture -------------------------------------------------------------------- */

#define FIXTURE_W 4
#define FIXTURE_H 3

static uint64_t g_buf[FIXTURE_W * FIXTURE_H * sizeof(rlk_tile_t) / sizeof(uint64_t) + 1];
static rlk_tilemap_t g_tm;

static void
fixture_setup(void) {
	rlk_tilemap_init(&g_tm, FIXTURE_W, FIXTURE_H, g_buf, sizeof(g_buf));
}

static void
fixture_teardown(void) {
	/* Nothing to do - buffer is static */
	(void)g_tm;
}

/* rlk_tilemap_init tests -------------------------------------------------------- */

static void
test_rlk_tilemap_init_valid(void) {
	fixture_setup();
	assert(g_tm.w == FIXTURE_W);
	assert(g_tm.h == FIXTURE_H);
	assert(g_tm.tiles == (rlk_tile_t *)g_buf);
	fixture_teardown();
}

static void
test_rlk_tilemap_init_buffer_alignment(void) {
	/* uint8_t buffer works: rlk_tile_t has no alignment requirement */
	uint8_t aligned_buf[2 * 2 * sizeof(rlk_tile_t)];
	rlk_tilemap_t tm;
	rlk_tilemap_init(&tm, 2, 2, aligned_buf, sizeof(aligned_buf));
	assert(tm.tiles != NULL);
}

/* rlk_tilemap_get tests ------------------------------------------------------- */

static void
test_rlk_tilemap_get_valid(void) {
	fixture_setup();
	rlk_tile_t *cell = rlk_tilemap_get(&g_tm, 2, 1);
	assert(cell != NULL);
	cell->type = 42;
	assert(cell->type == 42);
	fixture_teardown();
}

static void
test_rlk_tilemap_get_invalid(void) {
	fixture_setup();
	assert(rlk_tilemap_get(&g_tm, -1, 0) == NULL);
	assert(rlk_tilemap_get(&g_tm, 0, -1) == NULL);
	assert(rlk_tilemap_get(&g_tm, FIXTURE_W, 0) == NULL);
	assert(rlk_tilemap_get(&g_tm, 0, FIXTURE_H) == NULL);
	fixture_teardown();
}

static void
test_rlk_tilemap_get_const(void) {
	fixture_setup();
	/* get() can be assigned to const pointer without cast */
	const rlk_tile_t *cell = rlk_tilemap_get(&g_tm, 1, 1);
	assert(cell != NULL);
	assert(cell->type == 0);
	fixture_teardown();
}

/* Edge case tests ------------------------------------------------------ */

static void
test_rlk_tilemap_init_buffer_exact_size(void) {
	/* Buffer of exactly width*height tiles */
	rlk_tile_t buf[2 * 2];
	rlk_tilemap_t tm;
	rlk_tilemap_init(&tm, 2, 2, buf, sizeof(buf));
	assert(tm.w == 2);
	assert(tm.h == 2);
	assert(tm.tiles == buf);
}

/* Main ----------------------------------------------------------------------- */

int
main(void) {
	test_rlk_tilemap_init_valid();
	test_rlk_tilemap_init_buffer_alignment();

	/* rlk_tilemap_get tests (includes const assignment test) */
	test_rlk_tilemap_get_valid();
	test_rlk_tilemap_get_invalid();
	test_rlk_tilemap_get_const();

	/* Edge cases */
	test_rlk_tilemap_init_buffer_exact_size();

	return 0;
}
