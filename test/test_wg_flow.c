#include <assert.h>
#include <stdint.h>

#include "wg_flow.h"

/* Fixture */

#define FW 16
#define FH 16

static uint16_t g_fill[FW * FH];
static uint8_t g_dir[FW * FH];
static uint32_t g_acc[FW * FH];
static int g_order[FW * FH];
static uint64_t g_scratch[FW * FH];
static float g_elev_buf[FW * FH];

static rlk_wg_flow_t g_flow;
static rlk_grid_t g_elev;

/* Simple slope: elevation increases with i (eastward). */
static void
fixture_setup_slope(void) {
    g_elev = (rlk_grid_t){ .lattice = { .w = FW, .h = FH, .cellsize = 1, .origin_x = 0, .origin_y = 0 }, .v = g_elev_buf };
    for (int j = 0; j < FH; j++) {
        for (int i = 0; i < FW; i++) {
            g_elev.v[(size_t)j * (size_t)FW + (size_t)i] = (float)i;
        }
    }
    g_flow = (rlk_wg_flow_t){ .w = FW, .h = FH, .fill = g_fill, .dir = g_dir, .acc = g_acc, .order = g_order };
}

/* POD init via designated initializer */

static void
test_rlk_wg_flow_pod_init(void) {
    rlk_wg_flow_t f = { .w = FW, .h = FH, .fill = g_fill, .dir = g_dir, .acc = g_acc, .order = g_order };
    assert(f.w == FW);
    assert(f.h == FH);
    assert(f.fill == g_fill);
    assert(f.dir == g_dir);
    assert(f.acc == g_acc);
    assert(f.order == g_order);
    assert(f.lo == 0.0f);
    assert(f.built == 0);
}

/* quant */

static void
test_rlk_wg_flow_quant(void) {
    assert(rlk_wg_flow_quant(10.0f, 10.0f) == 0);
    assert(rlk_wg_flow_quant(10.5f, 10.0f) == 128);  /* 0.5 * 256 */
    assert(rlk_wg_flow_quant(11.0f, 10.0f) == 256);
}

/* step */

static void
test_rlk_wg_flow_step(void) {
    int dx, dy;
    rlk_wg_flow_step(0, &dx, &dy);  /* E */
    assert(dx == 1 && dy == 0);
    rlk_wg_flow_step(2, &dx, &dy);  /* S */
    assert(dx == 0 && dy == 1);
    rlk_wg_flow_step(4, &dx, &dy);  /* W */
    assert(dx == -1 && dy == 0);
    rlk_wg_flow_step(6, &dx, &dy);  /* N */
    assert(dx == 0 && dy == -1);
    rlk_wg_flow_step(1, &dx, &dy);  /* SE */
    assert(dx == 1 && dy == 1);
}

/* build on a simple slope */

static void
test_rlk_wg_flow_build_slope(void) {
    fixture_setup_slope();
    assert(rlk_wg_flow_build(&g_flow, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* Edge cells are outlets */
    assert(g_dir[0] == RLK_WG_FLOW_OUTLET);              /* (0,0) */
    assert(g_dir[(size_t)(FW - 1)] == RLK_WG_FLOW_OUTLET); /* (FW-1,0) */
    assert(g_dir[(size_t)(FH - 1) * FW] == RLK_WG_FLOW_OUTLET); /* (0,FH-1) */

    /* Interior cells flow westward (toward lower elevation):
       direction is W(4), NW(5), or SW(3) depending on flood propagation. */
    for (int j = 1; j < FH - 1; j++) {
        for (int i = 1; i < FW - 1; i++) {
            uint8_t d = g_dir[(size_t)j * (size_t)FW + (size_t)i];
            assert(d == 3 || d == 4 || d == 5);  /* SW, W, NW */
        }
    }
}

static void
test_rlk_wg_flow_build_accumulation(void) {
    fixture_setup_slope();
    assert(rlk_wg_flow_build(&g_flow, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* Every cell has acc >= 1 */
    for (int k = 0; k < FW * FH; k++)
        assert(g_acc[k] >= 1);

    /* Conservation: sum of acc at outlets = total number of cells.
       Outlets are edge cells (dir == RLK_WG_FLOW_OUTLET). */
    uint32_t total = 0;
    for (int j = 0; j < FH; j++) {
        for (int i = 0; i < FW; i++) {
            if (g_dir[(size_t)j * (size_t)FW + (size_t)i] == RLK_WG_FLOW_OUTLET)
                total += g_acc[(size_t)j * (size_t)FW + (size_t)i];
        }
    }
    assert(total == (uint32_t)(FW * FH));
}

/* build on a flat grid */

static void
test_rlk_wg_flow_build_flat(void) {
    float flat_buf[FW * FH];
    rlk_grid_t flat = { .lattice = { .w = FW, .h = FH, .cellsize = 1, .origin_x = 0, .origin_y = 0 }, .v = flat_buf };
    for (int k = 0; k < FW * FH; k++)
        flat.v[k] = 0.5f;

    g_flow = (rlk_wg_flow_t){ .w = FW, .h = FH, .fill = g_fill, .dir = g_dir, .acc = g_acc, .order = g_order };
    assert(rlk_wg_flow_build(&g_flow, &flat, -1.0f, g_scratch) == RLK_OK);

    /* All cells processed: no UNSET directions remain */
    for (int k = 0; k < FW * FH; k++)
        assert(g_dir[k] != RLK_WG_FLOW_UNSET);
}

/* build with sea: cells below sea level are outlets */

static void
test_rlk_wg_flow_build_sea(void) {
    fixture_setup_slope();
    /* Sea at elevation 5: cells with i < 5 are ocean */
    assert(rlk_wg_flow_build(&g_flow, &g_elev, 5.0f, g_scratch) == RLK_OK);

    /* Ocean cells are outlets */
    for (int j = 0; j < FH; j++) {
        for (int i = 0; i < 5; i++) {
            assert(g_dir[(size_t)j * (size_t)FW + (size_t)i] == RLK_WG_FLOW_OUTLET);
        }
    }
    /* Land cells (i >= 5) are not outlets */
    for (int j = 1; j < FH - 1; j++) {
        for (int i = 5; i < FW - 1; i++) {
            assert(g_dir[(size_t)j * (size_t)FW + (size_t)i] != RLK_WG_FLOW_OUTLET);
        }
    }
}

/* downstream */

static void
test_rlk_wg_flow_downstream(void) {
    fixture_setup_slope();
    assert(rlk_wg_flow_build(&g_flow, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* Interior cell (5,5) flows westward: downstream has i < 5 */
    int ni, nj;
    assert(rlk_wg_flow_downstream(&g_flow, 5, 5, &ni, &nj) == 1);
    assert(ni < 5);  /* west, possibly NW or SW */

    /* Edge cell (0,0) is an outlet: no downstream */
    assert(rlk_wg_flow_downstream(&g_flow, 0, 0, &ni, &nj) == 0);
}

/* fill_elev */

static void
test_rlk_wg_flow_fill_elev(void) {
    fixture_setup_slope();
    assert(rlk_wg_flow_build(&g_flow, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* On a monotonic slope, filled == original (no depressions) */
    for (int j = 0; j < FH; j++) {
        for (int i = 0; i < FW; i++) {
            float filled = rlk_wg_flow_fill_elev(&g_flow, i, j);
            float original = g_elev.v[(size_t)j * (size_t)FW + (size_t)i];
            assert(filled >= original - 0.01f);
        }
    }
}

/* lake_depth and is_lake on a pit */

static void
test_rlk_wg_flow_lake(void) {
    /* 5x5 grid with a pit at center: surrounded by higher cells */
    float pit_buf[25];
    rlk_grid_t pit = { .lattice = { .w = 5, .h = 5, .cellsize = 1, .origin_x = 0, .origin_y = 0 }, .v = pit_buf };
    for (int k = 0; k < 25; k++)
        pit.v[k] = 1.0f;
    pit.v[2 * 5 + 2] = 0.0f;  /* pit at (2,2) */

    uint16_t fill[25];
    uint8_t dir[25];
    uint32_t acc[25];
    int order[25];
    uint64_t scratch[25];

    rlk_wg_flow_t flow = { .w = 5, .h = 5, .fill = fill, .dir = dir, .acc = acc, .order = order };
    assert(rlk_wg_flow_build(&flow, &pit, -1.0f, scratch) == RLK_OK);

    /* The pit cell is filled up to the edge level (1.0) */
    float depth = rlk_wg_flow_lake_depth(&flow, &pit, -1.0f, 2, 2);
    assert(depth > 0.9f && depth < 1.1f);
    assert(rlk_wg_flow_is_lake(&flow, &pit, -1.0f, 0.5f, 2, 2) == 1);

    /* A cell on the slope is not a lake */
    assert(rlk_wg_flow_is_lake(&flow, &pit, -1.0f, 0.5f, 0, 0) == 0);
}

/* build error: elevation span too large */

static void
test_rlk_wg_flow_build_span_error(void) {
    float buf[4];
    rlk_grid_t g = { .lattice = { .w = 2, .h = 2, .cellsize = 1, .origin_x = 0, .origin_y = 0 }, .v = buf };
    g.v[0] = 0.0f;
    g.v[1] = 300.0f;  /* span > 256 */
    g.v[2] = 0.0f;
    g.v[3] = 0.0f;

    uint16_t fill[4];
    uint8_t dir[4];
    uint32_t acc[4];
    int order[4];
    uint64_t scratch[4];

    rlk_wg_flow_t flow = { .w = 2, .h = 2, .fill = fill, .dir = dir, .acc = acc, .order = order };
    assert(rlk_wg_flow_build(&flow, &g, -1.0f, scratch) == RLK_E_INVALID);
}

/* Main */

int
main(void) {
    test_rlk_wg_flow_pod_init();
    test_rlk_wg_flow_quant();
    test_rlk_wg_flow_step();
    test_rlk_wg_flow_build_slope();
    test_rlk_wg_flow_build_accumulation();
    test_rlk_wg_flow_build_flat();
    test_rlk_wg_flow_build_sea();
    test_rlk_wg_flow_downstream();
    test_rlk_wg_flow_fill_elev();
    test_rlk_wg_flow_lake();
    test_rlk_wg_flow_build_span_error();
    return 0;
}
