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
    rlk_fov_compute(&g_win, g_opaque, 8, 8, 100, g_visible);
    assert(count_visible() == FW * FH);
}

/* Radius 1: only the origin and its 4 cardinals (euclidean). */

static void
test_radius_1(void) {
    fixture();
    rlk_fov_compute(&g_win, g_opaque, 4, 4, 1, g_visible);
    assert(count_visible() == 5);
    assert(vis(4, 4) && vis(5, 4) && vis(3, 4) && vis(4, 5) && vis(4, 3));
    assert(!vis(5, 5));  /* diagonal at distance sqrt(2) > 1 */
}

/* Radius 2: the 13 cells with dx^2+dy^2 <= 4. */

static void
test_radius_2(void) {
    fixture();
    rlk_fov_compute(&g_win, g_opaque, 8, 8, 2, g_visible);
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
    rlk_fov_compute(&g_win, g_opaque, 5, 5, 100, g_visible);
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
    rlk_fov_compute(&g_win, NULL, 8, 8, 100, g_visible);
    assert(count_visible() == FW * FH);
}

/* The visible buffer is cleared on entry (pre-filled with 0xAA). */

static void
test_clears_visible(void) {
    fixture();
    for (int y = 0; y < FH; y++)
        g_opaque[(size_t)y * FW + 10] = 1;
    rlk_fov_compute(&g_win, g_opaque, 5, 5, 100, g_visible);
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
    rlk_fov_compute(&g_win, g_opaque, 8, 8, 6, g_visible);
    memcpy(copy, g_visible, sizeof(copy));
    rlk_fov_compute(&g_win, g_opaque, 8, 8, 6, g_visible);
    assert(memcmp(copy, g_visible, sizeof(copy)) == 0);
}

/* Negative window origin: world coords in and out. */

static void
test_negative_origin(void) {
    fixture();
    g_win.origin_x = -8;
    g_win.origin_y = -8;
    rlk_fov_compute(&g_win, g_opaque, -4, -4, 100, g_visible);
    assert(count_visible() == FW * FH);
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
    return 0;
}
