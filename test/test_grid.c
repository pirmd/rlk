#include <assert.h>
#include <string.h>

#include "grid.h"

/* Fixture */

#define FIXTURE_W 8
#define FIXTURE_H 4
#define FIXTURE_CELL 16
#define FIXTURE_ORIGIN_X (-1024)
#define FIXTURE_ORIGIN_Y (-1024)

static float g_buf[FIXTURE_W * FIXTURE_H];
static rlk_grid_t g_grid;

static void
fixture_setup(void) {
    g_grid = (rlk_grid_t){ .lattice = { .w = FIXTURE_W, .h = FIXTURE_H,
                                       .cellsize = FIXTURE_CELL,
                                       .origin_x = FIXTURE_ORIGIN_X, .origin_y = FIXTURE_ORIGIN_Y },
                           .values = g_buf };
    for (int k = 0; k < FIXTURE_W * FIXTURE_H; k++)
        g_buf[k] = 0.0f;
}

/* center */

static void
test_rlk_grid_center(void) {
    fixture_setup();

    float px, py;
    rlk_grid_center(&g_grid, 0, 0, &px, &py);
    assert(px == FIXTURE_ORIGIN_X + 0.5f * FIXTURE_CELL);
    assert(py == FIXTURE_ORIGIN_Y + 0.5f * FIXTURE_CELL);

    rlk_grid_center(&g_grid, 3, 2, &px, &py);
    assert(px == FIXTURE_ORIGIN_X + 3.5f * FIXTURE_CELL);
    assert(py == FIXTURE_ORIGIN_Y + 2.5f * FIXTURE_CELL);
}

/* cell_of */

static void
test_rlk_grid_cell_of_inside(void) {
    fixture_setup();

    int i, j;

    /* Centre of cell (0,0) */
    int wx = FIXTURE_ORIGIN_X + FIXTURE_CELL / 2;
    int wy = FIXTURE_ORIGIN_Y + FIXTURE_CELL / 2;
    assert(rlk_grid_cell_of(&g_grid, wx, wy, &i, &j) == 1);
    assert(i == 0 && j == 0);

    /* Centre of cell (2,1) */
    wx = FIXTURE_ORIGIN_X + 2 * FIXTURE_CELL + FIXTURE_CELL / 2;
    wy = FIXTURE_ORIGIN_Y + 1 * FIXTURE_CELL + FIXTURE_CELL / 2;
    assert(rlk_grid_cell_of(&g_grid, wx, wy, &i, &j) == 1);
    assert(i == 2 && j == 1);
}

static void
test_rlk_grid_cell_of_negative(void) {
    fixture_setup();

    int i, j;

    /* One tile left of the grid */
    assert(rlk_grid_cell_of(&g_grid, FIXTURE_ORIGIN_X - 1, FIXTURE_ORIGIN_Y, &i, &j) == 0);

    /* One tile above the grid */
    assert(rlk_grid_cell_of(&g_grid, FIXTURE_ORIGIN_X, FIXTURE_ORIGIN_Y - 1, &i, &j) == 0);

    /* Far negative: bounds-check before division must handle it */
    assert(rlk_grid_cell_of(&g_grid, FIXTURE_ORIGIN_X - FIXTURE_CELL - 1,
                            FIXTURE_ORIGIN_Y, &i, &j) == 0);
}

static void
test_rlk_grid_cell_of_outside(void) {
    fixture_setup();

    int i, j;

    /* Right of grid */
    int wx = FIXTURE_ORIGIN_X + FIXTURE_W * FIXTURE_CELL;
    int wy = FIXTURE_ORIGIN_Y + FIXTURE_CELL / 2;
    assert(rlk_grid_cell_of(&g_grid, wx, wy, &i, &j) == 0);

    /* Below grid */
    wx = FIXTURE_ORIGIN_X + FIXTURE_CELL / 2;
    wy = FIXTURE_ORIGIN_Y + FIXTURE_H * FIXTURE_CELL;
    assert(rlk_grid_cell_of(&g_grid, wx, wy, &i, &j) == 0);
}

/* sample */

static void
test_rlk_grid_get_center(void) {
    fixture_setup();

    /* Fill with known pattern: value = i + j * w */
    for (int j = 0; j < FIXTURE_H; j++) {
        for (int i = 0; i < FIXTURE_W; i++) {
            g_grid.values[j * FIXTURE_W + i] = (float)(i + j * FIXTURE_W);
        }
    }

    /* Sample at cell centres returns exact cell values */
    for (int j = 0; j < FIXTURE_H; j++) {
        for (int i = 0; i < FIXTURE_W; i++) {
            float px, py;
            rlk_grid_center(&g_grid, i, j, &px, &py);
            float expected = (float)(i + j * FIXTURE_W);
            assert(rlk_grid_get(&g_grid, px, py) == expected);
        }
    }
}

static void
test_rlk_grid_get_bilinear(void) {
    float buf[4];
    rlk_grid_t g = { .lattice = { .w = 2, .h = 2, .cellsize = 10,
                                  .origin_x = 0, .origin_y = 0 }, .values = buf };
    g.values[0] = 0.0f;  /* (0,0) */
    g.values[1] = 1.0f;  /* (1,0) */
    g.values[2] = 0.0f;  /* (0,1) */
    g.values[3] = 1.0f;  /* (1,1) */

    /* Centre of cell (0,0): world (5, 5) */
    assert(rlk_grid_get(&g, 5.0f, 5.0f) == 0.0f);

    /* Centre of cell (1,0): world (15, 5) */
    assert(rlk_grid_get(&g, 15.0f, 5.0f) == 1.0f);

    /* Midpoint between (0,0) and (1,0): world (10, 5) */
    assert(rlk_grid_get(&g, 10.0f, 5.0f) == 0.5f);
}

static void
test_rlk_grid_get_clamped(void) {
    fixture_setup();

    for (int j = 0; j < FIXTURE_H; j++) {
        for (int i = 0; i < FIXTURE_W; i++) {
            g_grid.values[j * FIXTURE_W + i] = (float)(i + j * FIXTURE_W);
        }
    }

    /* Left of grid: clamps to first column */
    float val = rlk_grid_get(&g_grid,
                                (float)FIXTURE_ORIGIN_X - 1.0f,
                                (float)FIXTURE_ORIGIN_Y + 0.5f * FIXTURE_CELL);
    assert(val == 0.0f);

    /* Right of grid: clamps to last column */
    val = rlk_grid_get(&g_grid,
                          (float)FIXTURE_ORIGIN_X + (float)FIXTURE_W * FIXTURE_CELL + 1.0f,
                          (float)FIXTURE_ORIGIN_Y + 0.5f * FIXTURE_CELL);
    assert(val == (float)(FIXTURE_W - 1));
}

/* minmax */

static void
test_rlk_grid_minmax(void) {
    fixture_setup();

    for (int j = 0; j < FIXTURE_H; j++) {
        for (int i = 0; i < FIXTURE_W; i++) {
            g_grid.values[j * FIXTURE_W + i] = (float)(i + j * FIXTURE_W);
        }
    }

    float lo, hi;
    rlk_grid_minmax(&g_grid, &lo, &hi);
    assert(lo == 0.0f);
    assert(hi == (float)((FIXTURE_W - 1) + (FIXTURE_H - 1) * FIXTURE_W));
}

static void
test_rlk_grid_minmax_single_value(void) {
    fixture_setup();

    for (int k = 0; k < FIXTURE_W * FIXTURE_H; k++)
        g_grid.values[k] = 42.0f;

    float lo, hi;
    rlk_grid_minmax(&g_grid, &lo, &hi);
    assert(lo == 42.0f);
    assert(hi == 42.0f);
}

/* Main */

int
main(void) {
    test_rlk_grid_center();
    test_rlk_grid_cell_of_inside();
    test_rlk_grid_cell_of_negative();
    test_rlk_grid_cell_of_outside();
    test_rlk_grid_get_center();
    test_rlk_grid_get_bilinear();
    test_rlk_grid_get_clamped();
    test_rlk_grid_minmax();
    test_rlk_grid_minmax_single_value();
    return 0;
}
