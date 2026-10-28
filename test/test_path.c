#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "path.h"
#include "tilemap.h"

/* Fixture: 8x8 tile-aligned window at world origin (0,0), all costs 1. */

#define PW 8
#define PH 8

static uint8_t g_table[PW * PH];
static int g_g[PW * PH], g_parent[PW * PH], g_heap[PW * PH], g_heap_pos[PW * PH];
static uint8_t g_closed[PW * PH];
static rlk_path_scratch_t g_scr;
static rlk_path_cost_t g_cost;
static int g_path[2 * PW * PH];

static void
fixture(void) {
    g_cost = (rlk_path_cost_t){
        .lattice = { .w = PW, .h = PH, .cellsize = 1, .origin_x = 0, .origin_y = 0 },
        .table = g_table,
    };
    g_scr = (rlk_path_scratch_t){
        .g = g_g, .parent = g_parent, .heap = g_heap,
        .heap_pos = g_heap_pos, .closed = g_closed,
    };
    memset(g_table, 1, sizeof(g_table));  /* every cell costs 1 */
}

static rlk_err_t
find(int sx, int sy, int gx, int gy, int *out, int cap, int *out_len) {
    return rlk_path_find(&g_cost, &g_scr, sx, sy, gx, gy, out, cap, out_len);
}

/* Open field: the diagonal is shortest (7 diagonal steps, cost 7*2 = 14). */

static void
test_diagonal_straight(void) {
    fixture();
    int n = 0;
    assert(find(0, 0, 7, 7, g_path, PW * PH, &n) == RLK_OK);
    assert(n == 8);
    assert(g_path[0] == 0 && g_path[1] == 0);
    assert(g_path[2 * (n - 1)] == 7 && g_path[2 * (n - 1) + 1] == 7);
    for (int k = 1; k < n; k++) {
        assert(g_path[2 * k] - g_path[2 * (k - 1)] == 1);
        assert(g_path[2 * k + 1] - g_path[2 * (k - 1) + 1] == 1);
    }
    assert(g_scr.g[7 * PW + 7] == 14);
}

/* Open field: cardinal step costs the target cell's enter cost. */

static void
test_cardinal_straight(void) {
    fixture();
    int n = 0;
    assert(find(0, 0, 7, 0, g_path, PW * PH, &n) == RLK_OK);
    assert(n == 8);
    assert(g_scr.g[7] == 7);
}

/* Wall column x=3 for y < 7 (gap at y=7): the path detours, never crosses. */

static void
test_wall_detour(void) {
    fixture();
    for (int y = 0; y < 7; y++)
        g_table[y * PW + 3] = RLK_PATH_BLOCKED;
    int n = 0;
    assert(find(0, 0, 6, 0, g_path, PW * PH, &n) == RLK_OK);
    assert(n > 8);  /* longer than the straight line */
    for (int k = 0; k < n; k++)
        assert(!(g_path[2 * k] == 3 && g_path[2 * k + 1] < 7));
}

/* Blocked goal -> RLK_E_NOTFOUND. */

static void
test_blocked_goal(void) {
    fixture();
    g_table[7 * PW + 7] = RLK_PATH_BLOCKED;
    int n = 0;
    assert(find(0, 0, 7, 7, g_path, PW * PH, &n) == RLK_E_NOTFOUND);
}

/* Goal walled in (cardinals + corner cutting): unreachable. */

static void
test_unreachable(void) {
    fixture();
    g_table[7 * PW + 6] = RLK_PATH_BLOCKED;  /* (6,7) */
    g_table[6 * PW + 7] = RLK_PATH_BLOCKED;  /* (7,6) */
    g_table[6 * PW + 6] = RLK_PATH_BLOCKED;  /* (6,6) */
    int n = 0;
    assert(find(0, 0, 7, 7, g_path, PW * PH, &n) == RLK_E_NOTFOUND);
}

/* Weighted field: row 0 costs 10, the rest costs 1. The cheapest path
   leaves row 0 (1 + 7 + 10 = 18) instead of crossing it (7*10 = 70). */

static void
test_weighted_detour(void) {
    fixture();
    for (int x = 0; x < PW; x++)
        g_table[x] = 10;  /* row y=0 */
    int n = 0;
    assert(find(0, 0, 7, 0, g_path, PW * PH, &n) == RLK_OK);
    assert(g_scr.g[7] == 18);
    assert(n > 8);
    for (int k = 1; k < n - 1; k++)
        assert(g_path[2 * k + 1] != 0);  /* no intermediate point on row 0 */
}

/* Coordinates outside the window -> RLK_E_INVALID. */

static void
test_out_of_window(void) {
    fixture();
    int n = 0;
    assert(find(-1, 0, 7, 7, g_path, PW * PH, &n) == RLK_E_INVALID);
    assert(find(0, 0, 8, 7, g_path, PW * PH, &n) == RLK_E_INVALID);
    assert(find(0, 0, 7, -1, g_path, PW * PH, &n) == RLK_E_INVALID);
}

/* cap smaller than the path length -> RLK_E_INVALID. */

static void
test_cap_too_small(void) {
    fixture();
    int n = 0;
    assert(find(0, 0, 7, 7, g_path, 2, &n) == RLK_E_INVALID);
}

/* start == goal: a single-point path. */

static void
test_start_equals_goal(void) {
    fixture();
    int n = 0;
    assert(find(3, 3, 3, 3, g_path, PW * PH, &n) == RLK_OK);
    assert(n == 1);
    assert(g_path[0] == 3 && g_path[1] == 3);
}

/* Deterministic: two runs give the same path. */

static void
test_determinism(void) {
    fixture();
    for (int y = 0; y < 7; y++)
        g_table[y * PW + 3] = RLK_PATH_BLOCKED;
    int path2[2 * PW * PH];
    int n1 = 0, n2 = 0;
    assert(find(0, 0, 6, 6, g_path, PW * PH, &n1) == RLK_OK);
    assert(find(0, 0, 6, 6, path2, PW * PH, &n2) == RLK_OK);
    assert(n1 == n2);
    assert(memcmp(g_path, path2, (size_t)n1 * 2 * sizeof(int)) == 0);
}

/* No corner cutting: a diagonal needs both adjacent cardinals enterable. */

static void
test_corner_cutting(void) {
    fixture();
    /* Both cardinals of the (0,0)->(1,1) diagonal are blocked: isolated. */
    g_table[0 * PW + 1] = RLK_PATH_BLOCKED;  /* (1,0) */
    g_table[1 * PW + 0] = RLK_PATH_BLOCKED;  /* (0,1) */
    int n = 0;
    assert(find(0, 0, 1, 1, g_path, PW * PH, &n) == RLK_E_NOTFOUND);

    /* Only one cardinal blocked: the diagonal is still refused, but the
       path goes around through the open cardinal. */
    fixture();
    g_table[0 * PW + 1] = RLK_PATH_BLOCKED;  /* (1,0) */
    assert(find(0, 0, 1, 1, g_path, PW * PH, &n) == RLK_OK);
    assert(n == 3);  /* (0,0) -> (0,1) -> (1,1) */
    assert(g_scr.g[1 * PW + 1] == 2);
}

/* Callback cost source: same result as the table; world coords, negative
   window origin. */

static int
cost_fn(void *ctx, int x, int y) {
    const uint8_t *t = (const uint8_t *)ctx;
    return t[(size_t)(y + 4) * (size_t)PW + (size_t)(x + 4)];
}

static void
test_callback(void) {
    fixture();
    g_cost.lattice.origin_x = -4;
    g_cost.lattice.origin_y = -4;
    g_cost.table = NULL;
    g_cost.fn = cost_fn;
    g_cost.ctx = g_table;
    int n = 0;
    assert(find(-4, -4, 3, 3, g_path, PW * PH, &n) == RLK_OK);
    assert(n == 8);
    assert(g_path[0] == -4 && g_path[1] == -4);
    assert(g_path[2 * (n - 1)] == 3 && g_path[2 * (n - 1) + 1] == 3);
    assert(g_scr.g[7 * PW + 7] == 14);  /* world (3,3) = cell (7,7) */
}

/* path_draw: paints the path tiles on a tilemap. */

static void
test_path_draw(void) {
    rlk_tile_t tiles[PW * PH];
    rlk_tilemap_t tm = { .w = PW, .h = PH, .origin_x = 0, .origin_y = 0,
                         .tiles = tiles };
    memset(tiles, 0, sizeof(tiles));
    const int path[6] = { 0, 0, 1, 0, 2, 0 };
    rlk_path_draw(&tm, path, 3, 1, (rlk_tile_t){ .type = 7 });
    for (int x = 0; x <= 2; x++)
        assert(tiles[x].type == 7);       /* row y=0 painted */
    assert(tiles[PW].type == 0);          /* row y=1 untouched */
}

/* Main */

int
main(void) {
    test_diagonal_straight();
    test_cardinal_straight();
    test_wall_detour();
    test_blocked_goal();
    test_unreachable();
    test_weighted_detour();
    test_out_of_window();
    test_cap_too_small();
    test_start_equals_goal();
    test_determinism();
    test_corner_cutting();
    test_callback();
    test_path_draw();
    return 0;
}
