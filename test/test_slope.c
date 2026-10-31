#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "slope.h"

#define SW 8
#define SH 6
#define ZSCALE 128.0f
#define GRAD   0.01f  /* elevation step per cell, eastward */

static float g_buf[SW * SH];
static rlk_grid_t g_grid;

static void
grid_init(void) {
    g_grid = (rlk_grid_t){ .lattice = { .w = SW, .h = SH, .cellsize = 4,
                                        .origin_x = 0, .origin_y = 0 }, .values = g_buf };
}

/* Flat grid: slope is exactly 0 everywhere. */

static void
test_flat(void) {
    for (int k = 0; k < SW * SH; k++)
        g_buf[k] = 0.5f;
    grid_init();
    for (int j = 0; j < SH; j++)
        for (int i = 0; i < SW; i++)
            assert(rlk_slope_get(&g_grid, i, j, ZSCALE) == 0.0f);
}

/* Constant eastward gradient: Horn 3x3 gives exactly zscale * GRAD / cellsize. */

static void
test_east_gradient(void) {
    for (int j = 0; j < SH; j++)
        for (int i = 0; i < SW; i++)
            g_buf[(size_t)j * (size_t)SW + (size_t)i] = (float)i * GRAD;
    grid_init();
    const float expect = ZSCALE * GRAD / 4.0f;  /* 0.32 */
    for (int j = 1; j < SH - 1; j++)
        for (int i = 1; i < SW - 1; i++)
            assert(fabsf(rlk_slope_get(&g_grid, i, j, ZSCALE) - expect) < 1e-5f);
}

/* zscale scales the slope linearly. */

static void
test_zscale(void) {
    for (int j = 0; j < SH; j++)
        for (int i = 0; i < SW; i++)
            g_buf[(size_t)j * (size_t)SW + (size_t)i] = (float)i * GRAD;
    grid_init();
    float s1 = rlk_slope_get(&g_grid, 4, 3, ZSCALE);
    float s2 = rlk_slope_get(&g_grid, 4, 3, 2.0f * ZSCALE);
    assert(fabsf(s2 - 2.0f * s1) < 1e-5f);
}

/* Edge cells replicate the out-of-grid column: the west sum is 0,
   the east sum is 4g -> dzdx = 1/2 of the interior. */

static void
test_edge_replicated(void) {
    for (int j = 0; j < SH; j++)
        for (int i = 0; i < SW; i++)
            g_buf[(size_t)j * (size_t)SW + (size_t)i] = (float)i * GRAD;
    grid_init();
    const float interior = ZSCALE * GRAD / 4.0f;
    const float edge = rlk_slope_get(&g_grid, 0, 3, ZSCALE);
    assert(fabsf(edge - 0.5f * interior) < 1e-5f);
}

/* Diagonal gradient: slope = sqrt(2) * zscale * GRAD / cellsize. */

static void
test_diagonal(void) {
    for (int j = 0; j < SH; j++)
        for (int i = 0; i < SW; i++)
            g_buf[(size_t)j * (size_t)SW + (size_t)i] = (float)(i + j) * GRAD;
    grid_init();
    const float expect = ZSCALE * GRAD / 4.0f * 1.4142135f;
    assert(fabsf(rlk_slope_get(&g_grid, 4, 3, ZSCALE) - expect) < 1e-3f);
}

/* Main */

int
main(void) {
    test_flat();
    test_east_gradient();
    test_zscale();
    test_edge_replicated();
    test_diagonal();
    return 0;
}
