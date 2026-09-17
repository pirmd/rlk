#include "test.h"
#include "view.h"

/* Fixture: a 10x8 grid with a distinct tile per cell. */
#define FX_W 10
#define FX_H 8

static rlk_grid_t  fx_grid;
static rlk_tile_t  fx_cells[FX_W * FX_H];

static void
fixture_setup(void) {
    fx_grid.width  = FX_W;
    fx_grid.height = FX_H;
    fx_grid.cells  = fx_cells;
    for (int y = 0; y < FX_H; ++y)
        for (int x = 0; x < FX_W; ++x)
            fx_cells[x + y * FX_W] = (rlk_tile_t)(x + y * FX_W);
}

static void
fixture_teardown(void) {
    fx_grid.cells = NULL;
}

/* --- rlk_view_init ------------------------------------------------------ */
static void
test_view_init(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_grid, 0, 0, 4, 4) == RLK_SUCCESS);
    ASSERT(v.grid == &fx_grid);
    ASSERT(v.ox == 0 && v.oy == 0 && v.vw == 4 && v.vh == 4);
    fixture_teardown();
}

static void
test_view_init_invalid(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, NULL, 0, 0, 4, 4) == RLK_E_INVALID_PARAM);
    ASSERT(rlk_view_init(&v, &fx_grid, 0, 0, 0, 4) == RLK_E_INVALID_PARAM);
    ASSERT(rlk_view_init(&v, &fx_grid, 0, 0, 4, 0) == RLK_E_INVALID_PARAM);
    ASSERT(rlk_view_init(NULL, &fx_grid, 0, 0, 4, 4) == RLK_E_INVALID_PARAM);
    fixture_teardown();
}

static void
test_view_init_clamps_origin(void) {
    fixture_setup();
    rlk_view_t v;
    /* origin overhanging bottom-right is pulled back inside */
    ASSERT(rlk_view_init(&v, &fx_grid, 100, 100, 4, 4) == RLK_SUCCESS);
    ASSERT(v.ox == FX_W - 4);
    ASSERT(v.oy == FX_H - 4);
    /* negative origin clamped to 0 */
    ASSERT(rlk_view_init(&v, &fx_grid, -5, -5, 4, 4) == RLK_SUCCESS);
    ASSERT(v.ox == 0 && v.oy == 0);
    fixture_teardown();
}

static void
test_view_init_oversized_shrinks(void) {
    fixture_setup();
    rlk_view_t v;
    /* view larger than grid is shrunk to the grid */
    ASSERT(rlk_view_init(&v, &fx_grid, 0, 0, 100, 100) == RLK_SUCCESS);
    ASSERT(v.vw == FX_W && v.vh == FX_H);
    fixture_teardown();
}

/* --- rlk_view_map / rlk_view_get ---------------------------------------- */
static void
test_view_map(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_grid, 2, 3, 4, 4) == RLK_SUCCESS);

    int gx, gy;
    ASSERT(rlk_view_map(&v, 0, 0, &gx, &gy));
    ASSERT(gx == 2 && gy == 3);
    ASSERT(rlk_view_map(&v, 3, 3, &gx, &gy));
    ASSERT(gx == 5 && gy == 6);

    /* view cell maps to a valid grid cell, so view_get returns that tile */
    ASSERT(rlk_view_get(&v, 0, 0) == fx_cells[2 + 3 * FX_W]);
    ASSERT(rlk_view_get(&v, 3, 3) == fx_cells[5 + 6 * FX_W]);
    fixture_teardown();
}

static void
test_view_get_out_of_view(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_grid, 0, 0, 4, 4) == RLK_SUCCESS);
    ASSERT(rlk_view_get(&v, -1, 0) == RLK_TILE_VOID);
    ASSERT(rlk_view_get(&v, 4, 0) == RLK_TILE_VOID);
    fixture_teardown();
}

/* --- rlk_view_center ---------------------------------------------------- */
static void
test_view_center_clamped(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_grid, 0, 0, 4, 4) == RLK_SUCCESS);

    /* center near top-left corner clamps to origin (0,0) */
    rlk_view_center(&v, 0, 0);
    ASSERT(v.ox == 0 && v.oy == 0);

    /* center near bottom-right corner clamps to max origin */
    rlk_view_center(&v, FX_W - 1, FX_H - 1);
    ASSERT(v.ox == FX_W - 4);
    ASSERT(v.oy == FX_H - 4);

    /* center in the middle maps the middle cell through view_get */
    rlk_view_center(&v, 5, 4);
    /* center cell of a 4x4 view is (vw/2, vh/2) */
    ASSERT(rlk_view_get(&v, v.vw / 2, v.vh / 2) == fx_cells[5 + 4 * FX_W]);
    fixture_teardown();
}

/* --- rlk_view_move ------------------------------------------------------ */
static void
test_view_move_clamped(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_grid, 0, 0, 4, 4) == RLK_SUCCESS);

    rlk_view_move(&v, 1, 1);
    ASSERT(v.ox == 1 && v.oy == 1);

    /* move past the bottom-right clamps to max origin */
    rlk_view_move(&v, 100, 100);
    ASSERT(v.ox == FX_W - 4 && v.oy == FX_H - 4);

    /* move past the top-left clamps to 0 */
    rlk_view_move(&v, -100, -100);
    ASSERT(v.ox == 0 && v.oy == 0);
    fixture_teardown();
}

/* --- Main ---------------------------------------------------------------- */
int
main(void) {
    test_view_init();
    test_view_init_invalid();
    test_view_init_clamps_origin();
    test_view_init_oversized_shrinks();
    test_view_map();
    test_view_get_out_of_view();
    test_view_center_clamped();
    test_view_move_clamped();
    return 0;
}
