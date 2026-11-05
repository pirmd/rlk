#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "fov.h"

/* Fixture: 16x16 tile-aligned window at world origin (0,0). */

#define FW 16
#define FH 16

static uint8_t g_opaque[FW * FH];
static uint8_t g_visible[FW * FH];
static rlk_lattice_t g_win;

static void
fixture(void) {
    g_win = (rlk_lattice_t){ .w = FW, .h = FH, .cellsize = 1,
                             .origin_x = 0, .origin_y = 0 };
    memset(g_opaque, 0, sizeof(g_opaque));
    memset(g_visible, 0xAA, sizeof(g_visible));  /* prove the call clears it */
}

static int
vis(int x, int y) {
    return g_visible[(size_t)y * FW + (size_t)x];
}

static int
count_visible(void) {
    int n = 0;
    for (int k = 0; k < FW * FH; k++)
        if (g_visible[k] == 1)
            n++;
    return n;
}

/* Open field, large radius: the whole window is visible. */

static void
test_open_field(void) {
    fixture();
    rlk_fov_compute(g_visible, &g_win, g_opaque, 8, 8, 100);
    assert(count_visible() == FW * FH);
}

/* Radius 1: only the origin and its 4 cardinals (euclidean). */

static void
test_radius_1(void) {
    fixture();
    rlk_fov_compute(g_visible, &g_win, g_opaque, 4, 4, 1);
    assert(count_visible() == 5);
    assert(vis(4, 4) && vis(5, 4) && vis(3, 4) && vis(4, 5) && vis(4, 3));
    assert(!vis(5, 5));  /* diagonal at distance sqrt(2) > 1 */
}

/* Radius 2: the 13 cells with dx^2+dy^2 <= 4. */

static void
test_radius_2(void) {
    fixture();
    rlk_fov_compute(g_visible, &g_win, g_opaque, 8, 8, 2);
    assert(count_visible() == 13);
    assert(!vis(10, 9));  /* dx=2, dy=1: 5 > 4 */
    assert(vis(10, 8));   /* dx=2, dy=0 */
    assert(vis(9, 9));    /* dx=1, dy=1 */
}

/* A full wall column casts a shadow: the wall is seen, what is behind is not. */

static void
test_wall_shadow(void) {
    fixture();
    for (int y = 0; y < FH; y++)
        g_opaque[(size_t)y * FW + 10] = 1;  /* column x=10 */
    rlk_fov_compute(g_visible, &g_win, g_opaque, 5, 5, 100);
    assert(vis(10, 5));    /* the wall itself is seen */
    assert(vis(10, 0));
    assert(!vis(11, 5));   /* behind the wall */
    assert(!vis(12, 5));
    assert(!vis(15, 15));  /* far corner, shadowed */
    assert(vis(9, 5));     /* in front of the wall */
}

/* opaque NULL: nothing blocks. */

static void
test_opaque_null(void) {
    fixture();
    rlk_fov_compute(g_visible, &g_win, NULL, 8, 8, 100);
    assert(count_visible() == FW * FH);
}

/* The visible buffer is cleared on entry (pre-filled with 0xAA). */

static void
test_clears_visible(void) {
    fixture();
    for (int y = 0; y < FH; y++)
        g_opaque[(size_t)y * FW + 10] = 1;
    rlk_fov_compute(g_visible, &g_win, g_opaque, 5, 5, 100);
    assert(!vis(12, 5));
    for (int k = 0; k < FW * FH; k++)
        assert(g_visible[k] == 0 || g_visible[k] == 1);  /* no 0xAA left */
}

/* Deterministic: two runs give the same buffer. */

static void
test_determinism(void) {
    fixture();
    g_opaque[5 * FW + 7] = 1;
    g_opaque[9 * FW + 3] = 1;
    g_opaque[2 * FW + 12] = 1;
    uint8_t copy[FW * FH];
    rlk_fov_compute(g_visible, &g_win, g_opaque, 8, 8, 6);
    memcpy(copy, g_visible, sizeof(copy));
    rlk_fov_compute(g_visible, &g_win, g_opaque, 8, 8, 6);
    assert(memcmp(copy, g_visible, sizeof(copy)) == 0);
}

/* Negative window origin: world coords in and out. */

static void
test_negative_origin(void) {
    fixture();
    g_win.origin_x = -8;
    g_win.origin_y = -8;
    rlk_fov_compute(g_visible, &g_win, g_opaque, -4, -4, 100);
    assert(count_visible() == FW * FH);
}

/* radius <= 0 means no distance limit: the whole open window is visible. */

static void
test_no_radius_limit(void) {
    fixture();
    rlk_fov_compute(g_visible, &g_win, g_opaque, 8, 8, 0);
    assert(count_visible() == FW * FH);
    rlk_fov_compute(g_visible, &g_win, g_opaque, 8, 8, -3);
    assert(count_visible() == FW * FH);
}

/* Origin in a window corner: only the quarter disc inside the window. */

static void
test_origin_corner(void) {
    fixture();
    rlk_fov_compute(g_visible, &g_win, g_opaque, 0, 0, 3);
    assert(vis(0, 0));
    assert(vis(3, 0) && vis(0, 3) && vis(2, 2));   /* dist^2 = 9, 9, 8 <= 9 */
    assert(!vis(3, 1) && !vis(4, 0) && !vis(15, 15)); /* dist^2 = 10, 16, 450 */
}

/* Banner example of fov.h: caller-provided opaque/visible buffers, one call. */

static void
test_example_rlk_fov_compute(void) {
    rlk_lattice_t win = { .w = FW, .h = FH, .cellsize = 1,
                          .origin_x = 0, .origin_y = 0 };
    uint8_t opaque[FW * FH] = { 0 };
    uint8_t visible[FW * FH];
    int ox = 8, oy = 8, radius = 100;
    rlk_fov_compute(visible, &win, opaque, ox, oy, radius);
    assert(visible[(size_t)oy * FW + (size_t)ox] == 1);
    for (int k = 0; k < FW * FH; k++)
        assert(visible[k] == 1);
}

/* Main */

int
main(void) {
    test_open_field();
    test_radius_1();
    test_radius_2();
    test_wall_shadow();
    test_opaque_null();
    test_clears_visible();
    test_determinism();
    test_negative_origin();
    test_no_radius_limit();
    test_origin_corner();
    test_example_rlk_fov_compute();
    return 0;
}
