#include <assert.h>
#include <string.h>

#include "wg_grid.h"

/* Fixture */

#define FIXTURE_W 8
#define FIXTURE_H 4
#define FIXTURE_CELL 16
#define FIXTURE_OX (-1024)
#define FIXTURE_OY (-1024)

static float g_buf[FIXTURE_W * FIXTURE_H];
static rlk_wg_grid_t g_grid;

static void
fixture_setup(void) {
    rlk_wg_grid_init(&g_grid, FIXTURE_W, FIXTURE_H, FIXTURE_CELL,
                     FIXTURE_OX, FIXTURE_OY, g_buf);
}

/* init */

static void
test_rlk_wg_grid_init(void) {
    fixture_setup();
    assert(g_grid.w == FIXTURE_W);
    assert(g_grid.h == FIXTURE_H);
    assert(g_grid.cell == FIXTURE_CELL);
    assert(g_grid.ox == FIXTURE_OX);
    assert(g_grid.oy == FIXTURE_OY);
    assert(g_grid.v == g_buf);

    /* All values zeroed */
    for (int k = 0; k < FIXTURE_W * FIXTURE_H; k++)
        assert(g_buf[k] == 0.0f);
}

/* Direct buffer access (POD: no getters/setters) */

static void
test_rlk_wg_grid_direct_access(void) {
    fixture_setup();

    /* Write via direct indexing */
    g_grid.v[1 * FIXTURE_W + 2] = 0.5f;
    assert(g_grid.v[1 * FIXTURE_W + 2] == 0.5f);

    g_grid.v[0] = 1.0f;
    assert(g_grid.v[0] == 1.0f);
    assert(g_grid.v[1 * FIXTURE_W + 2] == 0.5f);
}

/* center */

static void
test_rlk_wg_grid_center(void) {
    fixture_setup();

    float px, py;
    rlk_wg_grid_center(&g_grid, 0, 0, &px, &py);
    assert(px == FIXTURE_OX + 0.5f * FIXTURE_CELL);
    assert(py == FIXTURE_OY + 0.5f * FIXTURE_CELL);

    rlk_wg_grid_center(&g_grid, 3, 2, &px, &py);
    assert(px == FIXTURE_OX + 3.5f * FIXTURE_CELL);
    assert(py == FIXTURE_OY + 2.5f * FIXTURE_CELL);
}

/* cell_of */

static void
test_rlk_wg_grid_cell_of_inside(void) {
    fixture_setup();

    int i, j;

    /* Centre of cell (0,0) */
    int wx = FIXTURE_OX + FIXTURE_CELL / 2;
    int wy = FIXTURE_OY + FIXTURE_CELL / 2;
    assert(rlk_wg_grid_cell_of(&g_grid, wx, wy, &i, &j) == 1);
    assert(i == 0 && j == 0);

    /* Centre of cell (2,1) */
    wx = FIXTURE_OX + 2 * FIXTURE_CELL + FIXTURE_CELL / 2;
    wy = FIXTURE_OY + 1 * FIXTURE_CELL + FIXTURE_CELL / 2;
    assert(rlk_wg_grid_cell_of(&g_grid, wx, wy, &i, &j) == 1);
    assert(i == 2 && j == 1);
}

static void
test_rlk_wg_grid_cell_of_negative(void) {
    fixture_setup();

    int i, j;

    /* One tile left of the grid */
    assert(rlk_wg_grid_cell_of(&g_grid, FIXTURE_OX - 1, FIXTURE_OY, &i, &j) == 0);

    /* One tile above the grid */
    assert(rlk_wg_grid_cell_of(&g_grid, FIXTURE_OX, FIXTURE_OY - 1, &i, &j) == 0);

    /* Far negative: floor division must handle it */
    assert(rlk_wg_grid_cell_of(&g_grid, FIXTURE_OX - FIXTURE_CELL - 1,
                               FIXTURE_OY, &i, &j) == 0);
}

static void
test_rlk_wg_grid_cell_of_outside(void) {
    fixture_setup();

    int i, j;

    /* Right of grid */
    int wx = FIXTURE_OX + FIXTURE_W * FIXTURE_CELL;
    int wy = FIXTURE_OY + FIXTURE_CELL / 2;
    assert(rlk_wg_grid_cell_of(&g_grid, wx, wy, &i, &j) == 0);

    /* Below grid */
    wx = FIXTURE_OX + FIXTURE_CELL / 2;
    wy = FIXTURE_OY + FIXTURE_H * FIXTURE_CELL;
    assert(rlk_wg_grid_cell_of(&g_grid, wx, wy, &i, &j) == 0);
}

/* sample */

static void
test_rlk_wg_grid_sample_center(void) {
    fixture_setup();

    /* Fill with known pattern: value = i + j * w */
    for (int j = 0; j < FIXTURE_H; j++) {
        for (int i = 0; i < FIXTURE_W; i++) {
            g_grid.v[j * FIXTURE_W + i] = (float)(i + j * FIXTURE_W);
        }
    }

    /* Sample at cell centres returns exact cell values */
    for (int j = 0; j < FIXTURE_H; j++) {
        for (int i = 0; i < FIXTURE_W; i++) {
            float px, py;
            rlk_wg_grid_center(&g_grid, i, j, &px, &py);
            float expected = (float)(i + j * FIXTURE_W);
            assert(rlk_wg_grid_sample(&g_grid, px, py) == expected);
        }
    }
}

static void
test_rlk_wg_grid_sample_bilinear(void) {
    fixture_setup();

    /* 2x2 grid with known corners */
    float buf[4];
    rlk_wg_grid_t g;
    rlk_wg_grid_init(&g, 2, 2, 10, 0, 0, buf);
    g.v[0] = 0.0f;  /* (0,0) */
    g.v[1] = 1.0f;  /* (1,0) */
    g.v[2] = 0.0f;  /* (0,1) */
    g.v[3] = 1.0f;  /* (1,1) */

    /* Centre of cell (0,0): world (5, 5) */
    assert(rlk_wg_grid_sample(&g, 5.0f, 5.0f) == 0.0f);

    /* Centre of cell (1,0): world (15, 5) */
    assert(rlk_wg_grid_sample(&g, 15.0f, 5.0f) == 1.0f);

    /* Midpoint between (0,0) and (1,0): world (10, 5) */
    assert(rlk_wg_grid_sample(&g, 10.0f, 5.0f) == 0.5f);
}

static void
test_rlk_wg_grid_sample_clamped(void) {
    fixture_setup();

    for (int j = 0; j < FIXTURE_H; j++) {
        for (int i = 0; i < FIXTURE_W; i++) {
            g_grid.v[j * FIXTURE_W + i] = (float)(i + j * FIXTURE_W);
        }
    }

    /* Left of grid: clamps to first column */
    float val = rlk_wg_grid_sample(&g_grid,
                                   (float)FIXTURE_OX - 1.0f,
                                   (float)FIXTURE_OY + 0.5f * FIXTURE_CELL);
    assert(val == 0.0f);

    /* Right of grid: clamps to last column */
    val = rlk_wg_grid_sample(&g_grid,
                             (float)FIXTURE_OX + (float)FIXTURE_W * FIXTURE_CELL + 1.0f,
                             (float)FIXTURE_OY + 0.5f * FIXTURE_CELL);
    assert(val == (float)(FIXTURE_W - 1));
}

/* minmax */

static void
test_rlk_wg_grid_minmax(void) {
    fixture_setup();

    for (int j = 0; j < FIXTURE_H; j++) {
        for (int i = 0; i < FIXTURE_W; i++) {
            g_grid.v[j * FIXTURE_W + i] = (float)(i + j * FIXTURE_W);
        }
    }

    float lo, hi;
    rlk_wg_grid_minmax(&g_grid, &lo, &hi);
    assert(lo == 0.0f);
    assert(hi == (float)((FIXTURE_W - 1) + (FIXTURE_H - 1) * FIXTURE_W));
}

static void
test_rlk_wg_grid_minmax_single_value(void) {
    fixture_setup();

    for (int k = 0; k < FIXTURE_W * FIXTURE_H; k++)
        g_grid.v[k] = 42.0f;

    float lo, hi;
    rlk_wg_grid_minmax(&g_grid, &lo, &hi);
    assert(lo == 42.0f);
    assert(hi == 42.0f);
}

/* fill */

static float
fill_fn(float px, float py, const void *user) {
    (void)user;
    return px + py;
}

static void
test_rlk_wg_grid_fill(void) {
    fixture_setup();

    rlk_wg_grid_fill(&g_grid, fill_fn, NULL);

    float px, py;
    rlk_wg_grid_center(&g_grid, 0, 0, &px, &py);
    assert(g_grid.v[0] == px + py);

    rlk_wg_grid_center(&g_grid, 2, 1, &px, &py);
    assert(g_grid.v[1 * FIXTURE_W + 2] == px + py);
}

/* Main */

int
main(void) {
    test_rlk_wg_grid_init();
    test_rlk_wg_grid_direct_access();
    test_rlk_wg_grid_center();
    test_rlk_wg_grid_cell_of_inside();
    test_rlk_wg_grid_cell_of_negative();
    test_rlk_wg_grid_cell_of_outside();
    test_rlk_wg_grid_sample_center();
    test_rlk_wg_grid_sample_bilinear();
    test_rlk_wg_grid_sample_clamped();
    test_rlk_wg_grid_minmax();
    test_rlk_wg_grid_minmax_single_value();
    test_rlk_wg_grid_fill();
    return 0;
}
