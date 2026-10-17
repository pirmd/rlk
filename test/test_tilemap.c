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
	/* Nothing to do - buffer is on stack */
	(void)g_tm;
}

/* rlk_tilemap_init tests -------------------------------------------------------- */

static void
test_rlk_tilemap_init_valid(void) {
	fixture_setup();
	assert(g_tm.width == FIXTURE_W);
	assert(g_tm.height == FIXTURE_H);

	/* Verify all tiles are cleared (zero-initialized) */
	for (int y = 0; y < FIXTURE_H; y++) {
		for (int x = 0; x < FIXTURE_W; x++) {
			const rlk_tile_t *cell = rlk_tilemap_get(&g_tm, x, y);
			assert(cell != NULL);
			assert(cell->type == 0);
			assert(cell->flags == 0);
		}
	}
	fixture_teardown();
}

static void
test_rlk_tilemap_init_buffer_alignment(void) {
	/* Buffer must be large enough */
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
	/* Buffer exactly the required size for 2x2 tiles */
	const int w = 2, h = 2;
	/* 2*2*8 = 32 bytes, need 33 bytes to be exact, but align to uint64_t */
	uint64_t buf[4]; /* 4*8 = 32 bytes >= 2*2*8 = 16 bytes */
	rlk_tilemap_t tm;
	rlk_tilemap_init(&tm, w, h, buf, sizeof(buf));
	assert(tm.width == w);
	assert(tm.height == h);
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
