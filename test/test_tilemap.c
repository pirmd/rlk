#include <assert.h>
#include <string.h>
#include "tilemap.h"

#define F_WALKABLE 1u   /* game-defined flag bit (rlk reserves none) */

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

/* rlk_tilemap_lattice: tile-aligned lattice over the tilemap rect */

static void
test_rlk_tilemap_lattice(void) {
	rlk_tile_t buf[4 * 3];
	rlk_tilemap_t tm;
	rlk_tilemap_init(&tm, 4, 3, buf, sizeof(buf));
	tm.origin_x = 10;
	tm.origin_y = -5;
	rlk_lattice_t l = rlk_tilemap_lattice(&tm);
	assert(l.w == 4 && l.h == 3 && l.cellsize == 1);
	assert(l.origin_x == 10 && l.origin_y == -5);
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

/* rlk_tilemap_set tests ------------------------------------------------------- */

static void
test_rlk_tilemap_set_writes(void) {
	fixture_setup();
	memset(g_buf, 0, sizeof(g_buf));
	rlk_tile_t t = { .type = 7, .flags = F_WALKABLE, .biome = 2,
	                 .variant = 3, .depth = 1 };
	rlk_tilemap_set(&g_tm, 1, 1, t);
	const rlk_tile_t *back = rlk_tilemap_get(&g_tm, 1, 1);
	assert(back != NULL);
	assert(back->type == 7 && back->flags == F_WALKABLE
	       && back->biome == 2 && back->variant == 3 && back->depth == 1);
	fixture_teardown();
}

static void
test_rlk_tilemap_set_outside_ignored(void) {
	fixture_setup();
	memset(g_buf, 0, sizeof(g_buf));
	rlk_tile_t t = { .type = 9 };
	rlk_tilemap_set(&g_tm, -1, 0, t); /* no effect, no crash */
	rlk_tilemap_set(&g_tm, FIXTURE_W, FIXTURE_H, t);
	assert(rlk_tilemap_get(&g_tm, 0, 0)->type == 0);
	fixture_teardown();
}

/* rlk_tilemap_set_line tests ------------------------------------------------------ */

static void
test_rlk_tilemap_set_line_horizontal(void) {
	fixture_setup();
	memset(g_buf, 0, sizeof(g_buf));
	rlk_tile_t t = { .type = 5 };
	rlk_tilemap_set_line(&g_tm, 0, 1, 3, 1, 1, t);
	for (int x = 0; x < FIXTURE_W; x++)
		assert(rlk_tilemap_get(&g_tm, x, 1)->type == 5);
	assert(rlk_tilemap_get(&g_tm, 0, 0)->type == 0);
	assert(rlk_tilemap_get(&g_tm, 0, 2)->type == 0);
	fixture_teardown();
}

static void
test_rlk_tilemap_set_line_diagonal(void) {
	fixture_setup();
	memset(g_buf, 0, sizeof(g_buf));
	rlk_tile_t t = { .type = 5 };
	rlk_tilemap_set_line(&g_tm, 0, 0, 3, 2, 1, t);
	/* Bresenham pixels: (0,0) (1,1) (2,1) (3,2) */
	assert(rlk_tilemap_get(&g_tm, 0, 0)->type == 5);
	assert(rlk_tilemap_get(&g_tm, 1, 1)->type == 5);
	assert(rlk_tilemap_get(&g_tm, 2, 1)->type == 5);
	assert(rlk_tilemap_get(&g_tm, 3, 2)->type == 5);
	assert(rlk_tilemap_get(&g_tm, 0, 1)->type == 0);
	assert(rlk_tilemap_get(&g_tm, 3, 1)->type == 0);
	assert(rlk_tilemap_get(&g_tm, 2, 2)->type == 0);
	fixture_teardown();
}

static void
test_rlk_tilemap_set_line_brush_width3(void) {
	fixture_setup();
	memset(g_buf, 0, sizeof(g_buf));
	rlk_tile_t t = { .type = 5 };
	rlk_tilemap_set_line(&g_tm, 1, 1, 1, 1, 3, t); /* single point, 3x3 brush */
	for (int y = 0; y <= 2; y++)
		for (int x = 0; x <= 2; x++)
			assert(rlk_tilemap_get(&g_tm, x, y)->type == 5);
	assert(rlk_tilemap_get(&g_tm, 3, 1)->type == 0); /* outside the brush */
	fixture_teardown();
}

static void
test_rlk_tilemap_set_line_clipped(void) {
	fixture_setup();
	memset(g_buf, 0, sizeof(g_buf));
	rlk_tile_t t = { .type = 5 };
	rlk_tilemap_set_line(&g_tm, -2, 0, 1, 0, 1, t); /* clipped to the tilemap */
	assert(rlk_tilemap_get(&g_tm, 0, 0)->type == 5);
	assert(rlk_tilemap_get(&g_tm, 1, 0)->type == 5);
	assert(rlk_tilemap_get(&g_tm, 2, 0)->type == 0);
	assert(rlk_tilemap_get(&g_tm, 3, 0)->type == 0);
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
	test_rlk_tilemap_lattice();

	/* rlk_tilemap_get tests (includes const assignment test) */
	test_rlk_tilemap_get_valid();
	test_rlk_tilemap_get_invalid();
	test_rlk_tilemap_get_const();

	/* rlk_tilemap_set / rlk_tilemap_set_line tests */
	test_rlk_tilemap_set_writes();
	test_rlk_tilemap_set_outside_ignored();
	test_rlk_tilemap_set_line_horizontal();
	test_rlk_tilemap_set_line_diagonal();
	test_rlk_tilemap_set_line_brush_width3();
	test_rlk_tilemap_set_line_clipped();

	/* Edge cases */
	test_rlk_tilemap_init_buffer_exact_size();

	return 0;
}
