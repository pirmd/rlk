#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "hash.h"
#include "path.h"
#include "path_distance.h"

/* Fixture: 8x8 tile-aligned window at world origin (0,0), all costs 1. */

#define PW 8
#define PH 8

static uint8_t g_table[PW * PH];
static int g_g[PW * PH], g_parent[PW * PH], g_heap[PW * PH], g_heap_pos[PW * PH];
static rlk_path_distance_scratch_t g_scr;
static rlk_path_cost_t g_cost;
static int g_path[2 * PW * PH];

static void
fixture(void) {
    g_cost = (rlk_path_cost_t){
        .lattice = { .w = PW, .h = PH, .cellsize = 1, .origin_x = 0, .origin_y = 0 },
        .table = g_table,
    };
    g_scr = (rlk_path_distance_scratch_t){
        .g = g_g, .parent = g_parent, .heap = g_heap, .heap_pos = g_heap_pos,
    };
    memset(g_table, 1, sizeof(g_table));  /* every cell costs 1 */
}

static rlk_err_t
flood(const int *sources, int nsrc) {
    return rlk_path_distance_build(&g_cost, &g_scr, sources, nsrc);
}

static rlk_err_t
trace(int x, int y, int *out, int cap, int *out_len) {
    return rlk_path_distance_find(&g_cost, &g_scr, x, y, out, cap, out_len);
}

/* Single source on an open field: the far corner is 7 diagonal steps away
   (cost 7*3 = 21), and the trace from it walks straight back to the source. */

static void
test_single_source_open(void) {
    fixture();
    const int src[2] = { 0, 0 };
    assert(flood(src, 1) == RLK_OK);
    assert(g_g[7 * PW + 7] == 21);
    assert(g_g[7] == 14);            /* cardinal row: 7 * 2 */
    assert(g_g[0] == 0);
    assert(g_parent[0] == -1);       /* source has no parent */

    int n = 0;
    assert(trace(7, 7, g_path, PW * PH, &n) == RLK_OK);
    assert(n == 8);
    assert(g_path[0] == 7 && g_path[1] == 7);
    assert(g_path[2 * (n - 1)] == 0 && g_path[2 * (n - 1) + 1] == 0);
    for (int k = 1; k < n; k++) {    /* pure diagonal descent */
        assert(g_path[2 * k] - g_path[2 * (k - 1)] == -1);
        assert(g_path[2 * k + 1] - g_path[2 * (k - 1) + 1] == -1);
    }
}

/* Two sources: each cell attaches to the nearest one. (3,3) is closer to
   (0,0) (3 diagonals = 9) than to (7,7) (4 diagonals = 12); (6,6) is the
   other way round (3 vs 18). */

static void
test_two_sources_nearest(void) {
    fixture();
    const int src[4] = { 0, 0, 7, 7 };
    assert(flood(src, 2) == RLK_OK);
    assert(g_g[3 * PW + 3] == 9);
    assert(g_g[6 * PW + 6] == 3);
    int n = 0;
    assert(trace(3, 3, g_path, PW * PH, &n) == RLK_OK);
    assert(g_path[2 * (n - 1)] == 0 && g_path[2 * (n - 1) + 1] == 0);
    assert(trace(6, 6, g_path, PW * PH, &n) == RLK_OK);
    assert(g_path[2 * (n - 1)] == 7 && g_path[2 * (n - 1) + 1] == 7);
}

/* Wall column x=3 for y < 7 (gap at y=7): cells right of the wall are
   reachable only through the gap; the trace never crosses the wall. */

static void
test_wall_detour(void) {
    fixture();
    for (int y = 0; y < 7; y++)
        g_table[y * PW + 3] = RLK_PATH_BLOCKED;
    const int src[2] = { 0, 0 };
    assert(flood(src, 1) == RLK_OK);
    assert(g_g[0 * PW + 6] > 12);    /* longer than the straight line */
    int n = 0;
    assert(trace(6, 0, g_path, PW * PH, &n) == RLK_OK);
    assert(n > 1);
    assert(g_path[2 * (n - 1)] == 0 && g_path[2 * (n - 1) + 1] == 0);
    for (int k = 0; k < n; k++)
        assert(!(g_path[2 * k] == 3 && g_path[2 * k + 1] < 7));
}

/* A cell walled in on all 8 sides stays RLK_PATH_DISTANCE_INF. */

static void
test_unreachable(void) {
    fixture();
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy)
                g_table[(4 + dy) * PW + (4 + dx)] = RLK_PATH_BLOCKED;
    const int src[2] = { 0, 0 };
    assert(flood(src, 1) == RLK_OK);
    assert(g_g[4 * PW + 4] == RLK_PATH_DISTANCE_INF);
    assert(g_parent[4 * PW + 4] == -1);
    int n = 0;
    assert(trace(4, 4, g_path, PW * PH, &n) == RLK_E_NOTFOUND);
}

/* Blocked sources are skipped; if all are blocked the field stays INF. */

static void
test_blocked_sources(void) {
    fixture();
    g_table[0] = RLK_PATH_BLOCKED;
    g_table[1] = RLK_PATH_BLOCKED;
    const int src[4] = { 0, 0, 1, 0 };
    assert(flood(src, 2) == RLK_OK);
    for (int k = 0; k < PW * PH; k++)
        assert(g_g[k] == RLK_PATH_DISTANCE_INF);

    /* Mixed: one blocked, one valid -> the valid one floods. (0,0) is
       blocked so it stays unreachable; (3,0) is 4 diagonals + 3 cardinals
       from (7,7) = 4*3 + 3*2 = 18. */
    fixture();
    g_table[0] = RLK_PATH_BLOCKED;
    const int src2[4] = { 0, 0, 7, 7 };
    assert(flood(src2, 2) == RLK_OK);
    assert(g_g[7 * PW + 7] == 0);
    assert(g_g[0] == RLK_PATH_DISTANCE_INF);
    assert(g_g[0 * PW + 3] == 18);
}

/* nsrc == 0 -> OK, everything unreachable. */

static void
test_no_sources(void) {
    fixture();
    assert(flood(NULL, 0) == RLK_OK);
    for (int k = 0; k < PW * PH; k++)
        assert(g_g[k] == RLK_PATH_DISTANCE_INF);
}

/* A source outside the window -> RLK_E_INVALID (same for trace). */

static void
test_out_of_window(void) {
    fixture();
    const int src[2] = { -1, 0 };
    assert(flood(src, 1) == RLK_E_INVALID);
    const int src2[2] = { 0, 8 };
    assert(flood(src2, 1) == RLK_E_INVALID);
    int n = 0;
    assert(trace(8, 0, g_path, PW * PH, &n) == RLK_E_INVALID);
    assert(trace(0, -1, g_path, PW * PH, &n) == RLK_E_INVALID);
}

/* Trace from a source itself: single-point path. cap < len -> INVALID,
   cap == len accepted. */

static void
test_trace_edges(void) {
    fixture();
    const int src[2] = { 0, 0 };
    assert(flood(src, 1) == RLK_OK);
    int n = 0;
    assert(trace(0, 0, g_path, PW * PH, &n) == RLK_OK);
    assert(n == 1);
    assert(g_path[0] == 0 && g_path[1] == 0);
    assert(trace(7, 7, g_path, 2, &n) == RLK_E_INVALID);
    assert(trace(7, 7, g_path, 8, &n) == RLK_OK);
    assert(n == 8);
}

/* Callback cost source with a negative window origin: same field as the
   table version. */

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
    const int src[2] = { -4, -4 };
    assert(flood(src, 1) == RLK_OK);
    assert(g_g[7 * PW + 7] == 21);   /* world (3,3) = cell (7,7) */
    int n = 0;
    assert(trace(3, 3, g_path, PW * PH, &n) == RLK_OK);
    assert(n == 8);
    assert(g_path[0] == 3 && g_path[1] == 3);
    assert(g_path[2 * (n - 1)] == -4 && g_path[2 * (n - 1) + 1] == -4);
}

/* Property: on random weighted grids with 3 sources, the field equals a
   reference multi-source Dijkstra, and every traced path is valid (legal
   steps, no corner cutting, ends on a source) with cost == g[cell]. */

#define OW 16
#define OH 16

static void
ref_dijkstra_multi(const uint8_t *t, const int *src, int nsrc, int *dist) {
    static const int dx[8] = { 1, 0, -1, 0, 1, -1, 1, -1 };
    static const int dy[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
    uint8_t done[OW * OH];
    for (int k = 0; k < OW * OH; k++) { dist[k] = 1 << 28; done[k] = 0; }
    for (int s = 0; s < nsrc; s++) {
        int si = src[2 * s + 1] * OW + src[2 * s];
        if (t[si] != RLK_PATH_BLOCKED)
            dist[si] = 0;
    }
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
                step = 3 * c;
            } else step = 2 * c;
            int nxt = nj * OW + ni;
            if (dist[best] + step < dist[nxt]) dist[nxt] = dist[best] + step;
        }
    }
}

static void
test_field_vs_dijkstra(void) {
    for (int t = 0; t < 40; t++) {
        uint8_t tab[OW * OH];
        for (int k = 0; k < OW * OH; k++) {
            uint64_t h = rlk_hash_at((uint64_t)t + 1, k, 0, 0xBEEFULL);
            tab[k] = (uint8_t)((h % 100) < 15 ? RLK_PATH_BLOCKED
                                              : 1 + (h >> 8) % 20);
        }
        int src[6];
        for (int s = 0; s < 3; s++) {
            uint64_t h = rlk_hash_at((uint64_t)t + 1, s, 7, 0xCAFEULL);
            src[2 * s]     = (int)((h >> 16) % OW);
            src[2 * s + 1] = (int)((h >> 48) % OH);
        }

        rlk_lattice_t lat = { .w = OW, .h = OH, .cellsize = 1,
                              .origin_x = 0, .origin_y = 0 };
        rlk_path_cost_t cost = { .lattice = lat, .table = tab };
        int gg[OW * OH], pp[OW * OH], hh[OW * OH], hp[OW * OH];
        rlk_path_distance_scratch_t scr = { gg, pp, hh, hp };
        assert(rlk_path_distance_build(&cost, &scr, src, 3) == RLK_OK);

        int ref[OW * OH];
        ref_dijkstra_multi(tab, src, 3, ref);
        for (int k = 0; k < OW * OH; k++) {
            if (ref[k] >= (1 << 28))
                assert(gg[k] == RLK_PATH_DISTANCE_INF);
            else
                assert(gg[k] == ref[k]);
        }

        /* Trace a sample of cells: path validity + cost == g. */
        for (int s = 0; s < 8; s++) {
            uint64_t h = rlk_hash_at((uint64_t)t + 1, s, 13, 0xF00DULL);
            int x = (int)((h >> 16) % OW), y = (int)((h >> 48) % OH);
            int path[2 * OW * OH];
            int n = 0;
            rlk_err_t e = rlk_path_distance_find(&cost, &scr, x, y, path,
                                              OW * OH, &n);
            int gi = y * OW + x;
            if (gg[gi] == RLK_PATH_DISTANCE_INF) {
                assert(e == RLK_E_NOTFOUND);
                continue;
            }
            assert(e == RLK_OK);
            assert(n >= 1 && path[0] == x && path[1] == y);
            int sum = 0;
            for (int k = 1; k < n; k++) {
                /* Trace descends toward the source: forward edge
                   out[k] -> out[k-1] costs the enter cost of out[k-1]. */
                int fx = path[2 * (k - 1)], fy = path[2 * (k - 1) + 1];
                int tx = path[2 * k], ty = path[2 * k + 1];
                int dx = tx - fx, dy = ty - fy;
                assert(dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
                assert(dx != 0 || dy != 0);
                assert(tab[ty * OW + tx] != RLK_PATH_BLOCKED);
                int c = tab[fy * OW + fx];
                if (dx != 0 && dy != 0) {
                    /* no corner cutting on the forward edge out[k]->out[k-1] */
                    assert(tab[fy * OW + tx] != RLK_PATH_BLOCKED);
                    assert(tab[ty * OW + fx] != RLK_PATH_BLOCKED);
                    sum += 3 * c;
                } else {
                    sum += 2 * c;
                }
            }
            assert(sum == gg[gi]);       /* traced cost == field value */
            /* Last point is a source (g == 0, parent == -1). */
            int lx = path[2 * (n - 1)], ly = path[2 * (n - 1) + 1];
            assert(gg[ly * OW + lx] == 0);
            assert(pp[ly * OW + lx] == -1);
        }
    }
}

/* Deterministic: two floods give identical g and parent arrays. */

static void
test_determinism(void) {
    fixture();
    for (int y = 0; y < 7; y++)
        g_table[y * PW + 3] = RLK_PATH_BLOCKED;
    g_table[2 * PW + 5] = 7;
    const int src[4] = { 0, 0, 7, 7 };
    assert(flood(src, 2) == RLK_OK);
    int g2[PW * PH], p2[PW * PH];
    memcpy(g2, g_g, sizeof(g2));
    memcpy(p2, g_parent, sizeof(p2));
    assert(flood(src, 2) == RLK_OK);
    assert(memcmp(g_g, g2, sizeof(g_g)) == 0);
    assert(memcmp(g_parent, p2, sizeof(g_parent)) == 0);
}

/* The usage example from the path_distance.h banner: declare the scratch
   buffers, flood from sources, then trace. */

static void
test_example_rlk_path_distance_build(void) {
    fixture();
    int sources[2] = { 0, 0 };
    rlk_err_t e = rlk_path_distance_build(&g_cost, &g_scr, sources, 1);
    assert(e == RLK_OK);
    int n = 0;
    e = rlk_path_distance_find(&g_cost, &g_scr, 7, 7, g_path, PW * PH, &n);
    assert(e == RLK_OK);
    assert(n == 8);
    assert(g_path[0] == 7 && g_path[1] == 7);
    assert(g_path[2 * (n - 1)] == 0 && g_path[2 * (n - 1) + 1] == 0);
}

/* Main */

int
main(void) {
    test_single_source_open();
    test_two_sources_nearest();
    test_wall_detour();
    test_unreachable();
    test_blocked_sources();
    test_no_sources();
    test_out_of_window();
    test_trace_edges();
    test_callback();
    test_field_vs_dijkstra();
    test_determinism();
    test_example_rlk_path_distance_build();
    return 0;
}
