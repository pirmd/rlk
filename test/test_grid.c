#include "test.h"
#include "grid.h"

/* --- helpers ------------------------------------------------------------- */
static void
assert_all_equal(const rlk_grid_t *g, rlk_tile_t want) {
    for (int y = 0; y < g->height; ++y)
        for (int x = 0; x < g->width; ++x)
            ASSERT(rlk_grid_get(g, x, y) == want);
}

/* --- rlk_grid_init ------------------------------------------------------- */
static void
test_grid_init_invalid(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 0, 10) == RLK_E_INVALID_PARAM);
    ASSERT(rlk_grid_init(&g, 10, 0) == RLK_E_INVALID_PARAM);
    ASSERT(rlk_grid_init(&g, -1, 10) == RLK_E_INVALID_PARAM);
    ASSERT(rlk_grid_init(NULL, 10, 10) == RLK_E_INVALID_PARAM);
}

static void
test_grid_init_valid(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 5, 7) == RLK_SUCCESS);
    ASSERT(g.width == 5 && g.height == 7);
    ASSERT(rlk_grid_size(&g) == 35);
    assert_all_equal(&g, RLK_TILE_VOID);
    rlk_grid_deinit(&g);
    ASSERT(g.cells == NULL && g.width == 0 && g.height == 0);
}

/* --- rlk_grid_in / rlk_grid_get / rlk_grid_set ---------------------------- */
static void
test_grid_in(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 4, 3) == RLK_SUCCESS);
    ASSERT(rlk_grid_in(&g, 0, 0) && rlk_grid_in(&g, 3, 2));
    ASSERT(!rlk_grid_in(&g, -1, 0));
    ASSERT(!rlk_grid_in(&g, 4, 0));
    ASSERT(!rlk_grid_in(&g, 0, 3));
    rlk_grid_deinit(&g);
}

static void
test_grid_get_set(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 4, 3) == RLK_SUCCESS);

    rlk_grid_set(&g, 1, 1, 42);
    ASSERT(rlk_grid_get(&g, 1, 1) == 42);
    /* untouched cells stay void */
    ASSERT(rlk_grid_get(&g, 0, 0) == RLK_TILE_VOID);

    /* out-of-bounds set is a no-op: only (1,1) should hold a value */
    rlk_grid_set(&g, -1, 0, 99);
    rlk_grid_set(&g, 4, 0, 99);
    rlk_grid_set(&g, 0, 3, 99);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x) {
            rlk_tile_t want = (x == 1 && y == 1) ? 42 : RLK_TILE_VOID;
            ASSERT(rlk_grid_get(&g, x, y) == want);
        }

    rlk_grid_deinit(&g);
}

static void
test_grid_get_out_of_bounds(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 2, 2) == RLK_SUCCESS);
    ASSERT(rlk_grid_get(&g, -1, -1) == RLK_TILE_VOID);
    ASSERT(rlk_grid_get(&g, 2, 2) == RLK_TILE_VOID);
    rlk_grid_deinit(&g);
}

/* --- rlk_grid_fill ------------------------------------------------------- */
static void
test_grid_fill(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 3, 3) == RLK_SUCCESS);
    rlk_grid_fill(&g, 7);
    assert_all_equal(&g, 7);
    /* fill with void clears back to zero */
    rlk_grid_fill(&g, RLK_TILE_VOID);
    assert_all_equal(&g, RLK_TILE_VOID);
    rlk_grid_deinit(&g);
}

/* --- rlk_grid_fill_rect -------------------------------------------------- */
static void
test_grid_fill_rect(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 5, 5) == RLK_SUCCESS);
    rlk_grid_fill_rect(&g, 1, 1, 3, 3, 9);

    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            rlk_tile_t want = (x >= 1 && x < 4 && y >= 1 && y < 4) ? 9
                                                                 : RLK_TILE_VOID;
            ASSERT(rlk_grid_get(&g, x, y) == want);
        }
    rlk_grid_deinit(&g);
}

static void
test_grid_fill_rect_clipped(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 4, 4) == RLK_SUCCESS);
    /* region overhanging the top-left corner: only [0,2)x[0,2) filled */
    rlk_grid_fill_rect(&g, -2, -2, 4, 4, 1);
    ASSERT(rlk_grid_get(&g, 0, 0) == 1);
    ASSERT(rlk_grid_get(&g, 1, 1) == 1);
    ASSERT(rlk_grid_get(&g, 2, 2) == RLK_TILE_VOID);
    rlk_grid_deinit(&g);
}

static void
test_grid_fill_rect_empty(void) {
    rlk_grid_t g;
    ASSERT(rlk_grid_init(&g, 3, 3) == RLK_SUCCESS);
    rlk_grid_fill_rect(&g, 0, 0, 0, 3, 1);  /* w==0 */
    rlk_grid_fill_rect(&g, 0, 0, 3, 0, 1);  /* h==0 */
    assert_all_equal(&g, RLK_TILE_VOID);
    rlk_grid_deinit(&g);
}

/* --- deinit safety ------------------------------------------------------- */
static void
test_grid_deinit_safe(void) {
    rlk_grid_deinit(NULL);  /* must not crash */
    rlk_grid_t g = {0};
    rlk_grid_deinit(&g);    /* must not double-free */
}

/* --- Main ---------------------------------------------------------------- */
int
main(void) {
    test_grid_init_invalid();
    test_grid_init_valid();
    test_grid_in();
    test_grid_get_set();
    test_grid_get_out_of_bounds();
    test_grid_fill();
    test_grid_fill_rect();
    test_grid_fill_rect_clipped();
    test_grid_fill_rect_empty();
    test_grid_deinit_safe();
    return 0;
}
