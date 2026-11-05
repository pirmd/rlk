#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "hash.h"
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

/* Property: on random weighted grids, rlk_path_find returns a valid path
   whose cost equals a reference Dijkstra with the same movement rules. */

#define OW 16
#define OH 16

static int
ref_dijkstra(const uint8_t *t, int si, int gi) {
    static const int dx[8] = { 1, 0, -1, 0, 1, -1, 1, -1 };
    static const int dy[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
    int dist[OW * OH];
    uint8_t done[OW * OH];
    for (int k = 0; k < OW * OH; k++) { dist[k] = 1 << 28; done[k] = 0; }
    dist[si] = 0;
    for (;;) {
        int best = -1, bd = 1 << 28;
        for (int k = 0; k < OW * OH; k++)
            if (!done[k] && dist[k] < bd) { bd = dist[k]; best = k; }
        if (best < 0) break;
        done[best] = 1;
        int ci = best % OW, cj = best / OW;
        for (int d = 0; d < 8; d++) {
            int ni = ci + dx[d], nj = cj + dy[d];
            if (ni < 0 || nj < 0 || ni >= OW || nj >= OH) continue;
            int c = t[nj * OW + ni];
            if (c == RLK_PATH_BLOCKED) continue;
            int step;
            if (dx[d] != 0 && dy[d] != 0) {
                if (t[cj * OW + ci + dx[d]] == RLK_PATH_BLOCKED) continue;
                if (t[(cj + dy[d]) * OW + ci] == RLK_PATH_BLOCKED) continue;
                step = (3 * c + 1) / 2;
            } else step = c;
            int nxt = nj * OW + ni;
            if (dist[best] + step < dist[nxt]) dist[nxt] = dist[best] + step;
        }
    }
    return dist[gi];
}

static void
test_optimal_vs_dijkstra(void) {
    for (int t = 0; t < 40; t++) {
        uint8_t tab[OW * OH];
        for (int k = 0; k < OW * OH; k++) {
            uint64_t h = rlk_hash_at((uint64_t)t + 1, k, 0, 0xBEEFULL);
            tab[k] = (uint8_t)((h % 100) < 15 ? RLK_PATH_BLOCKED
                                              : 1 + (h >> 8) % 20);
        }
        uint64_t h0 = rlk_hash_at((uint64_t)t + 1, 0, 1, 0xBEEFULL);
        int sx = (int)((h0 >> 16) % OW), sy = (int)((h0 >> 32) % OH);
        int gx = (int)((h0 >> 48) % OW), gy = (int)(h0 % OH);

        rlk_lattice_t lat = { .w = OW, .h = OH, .cellsize = 1,
                              .origin_x = 0, .origin_y = 0 };
        rlk_path_cost_t cost = { .lattice = lat, .table = tab };
        int gg[OW * OH], pp[OW * OH], hh[OW * OH], hp[OW * OH];
        uint8_t cl[OW * OH];
        rlk_path_scratch_t scr = { gg, pp, hh, hp, cl };
        int path[2 * OW * OH];
        int n = 0;
        rlk_err_t e = rlk_path_find(&cost, &scr, sx, sy, gx, gy, path,
                                    OW * OH, &n);
        int si = sy * OW + sx, gi = gy * OW + gx;
        int ref = ref_dijkstra(tab, si, gi);
        if (e == RLK_OK) {
            assert(ref < (1 << 28));
            assert(gg[gi] == ref);  /* optimal cost */
            assert(n >= 1 && path[0] == sx && path[1] == sy);
            assert(path[2 * (n - 1)] == gx && path[2 * (n - 1) + 1] == gy);
            int sum = 0;
            for (int k = 0; k < n; k++) {
                int x = path[2 * k], y = path[2 * k + 1];
                if (k == 0) continue;
                assert(tab[y * OW + x] != RLK_PATH_BLOCKED);
                int dx = x - path[2 * (k - 1)], dy = y - path[2 * (k - 1) + 1];
                assert(dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
                assert(dx != 0 || dy != 0);
                int c = tab[y * OW + x];
                if (dx != 0 && dy != 0) {
                    /* no corner cutting */
                    assert(tab[path[2 * (k - 1) + 1] * OW + x] != RLK_PATH_BLOCKED);
                    assert(tab[y * OW + path[2 * (k - 1)]] != RLK_PATH_BLOCKED);
                    sum += (3 * c + 1) / 2;
                } else {
                    sum += c;
                }
            }
            assert(sum == gg[gi]);  /* rendered path cost == g[goal] */
        } else {
            assert(e == RLK_E_NOTFOUND);
            assert(ref >= (1 << 28));
        }
    }
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
    /* Boundary: cap == path length is accepted. */
    assert(find(0, 0, 7, 7, g_path, 8, &n) == RLK_OK);
    assert(n == 8);
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

    /* Width 3 brush: 3x3 block around each path tile. */
    memset(tiles, 0, sizeof(tiles));
    rlk_path_draw(&tm, path, 3, 3, (rlk_tile_t){ .type = 9 });
    assert(tiles[3].type == 9);           /* (3,0): brush edge */
    assert(tiles[PW + 3].type == 9);      /* (3,1): brush row below */
    assert(tiles[4].type == 0);           /* (4,0): outside brush */
    assert(tiles[2 * PW].type == 0);      /* (0,2): outside brush */
}

/* The usage example from the path.h banner: declare the scratch buffers,
   then find. */

static void
test_example_rlk_path_find(void) {
    fixture();
    int n = 0;
    rlk_err_t e = rlk_path_find(&g_cost, &g_scr, 0, 0, 7, 7,
                                g_path, PW * PH, &n);
    assert(e == RLK_OK);
    assert(n == 8);
    assert(g_path[0] == 0 && g_path[1] == 0);
    assert(g_path[2 * (n - 1)] == 7 && g_path[2 * (n - 1) + 1] == 7);
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
    test_optimal_vs_dijkstra();
    test_callback();
    test_path_draw();
    test_example_rlk_path_find();
    return 0;
}
