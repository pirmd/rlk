#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "drain.h"

/* Fixture */

#define FW 16
#define FH 16

static uint16_t g_filled[FW * FH];
static uint8_t g_dir[FW * FH];
static uint32_t g_acc[FW * FH];
static int g_order[FW * FH];
static uint64_t g_scratch[FW * FH];
static float g_elev_buf[FW * FH];

static rlk_drain_t g_drain;
static rlk_grid_t g_elev;

/* Simple slope: elevation increases with i (eastward). */
static void
fixture_setup_slope(void) {
    g_elev = (rlk_grid_t){ .lattice = { .w = FW, .h = FH, .cellsize = 1,
                                        .origin_x = 0, .origin_y = 0 }, .v = g_elev_buf };
    for (int j = 0; j < FH; j++) {
        for (int i = 0; i < FW; i++) {
            g_elev.v[(size_t)j * (size_t)FW + (size_t)i] = (float)i;
        }
    }
    g_drain = (rlk_drain_t){ .lattice = g_elev.lattice, .filled = g_filled,
                             .dir = g_dir, .acc = g_acc, .order = g_order };
}

/* POD init via designated initializer */

static void
test_rlk_drain_pod_init(void) {
    rlk_drain_t d = { .lattice = { .w = FW, .h = FH, .cellsize = 1,
                                   .origin_x = 0, .origin_y = 0 },
                      .filled = g_filled, .dir = g_dir,
                      .acc = g_acc, .order = g_order };
    assert(d.lattice.w == FW);
    assert(d.lattice.h == FH);
    assert(d.filled == g_filled);
    assert(d.dir == g_dir);
    assert(d.acc == g_acc);
    assert(d.order == g_order);
}

/* quant (absolute: e * RLK_DRAIN_SCALE, clamped below the sentinel) */

static void
test_rlk_drain_quant(void) {
    assert(rlk_drain_quant(0.0f) == 0);
    assert(rlk_drain_quant(0.5f) == 128);   /* 0.5 * 256 */
    assert(rlk_drain_quant(1.0f) == 256);
    assert(rlk_drain_quant(300.0f) == 65534);  /* clamped: 0xFFFF is the sentinel */
}

/* step */

static void
test_rlk_drain_step(void) {
    int dx, dy;
    rlk_drain_step(0, &dx, &dy);  /* E */
    assert(dx == 1 && dy == 0);
    rlk_drain_step(2, &dx, &dy);  /* S */
    assert(dx == 0 && dy == 1);
    rlk_drain_step(4, &dx, &dy);  /* W */
    assert(dx == -1 && dy == 0);
    rlk_drain_step(6, &dx, &dy);  /* N */
    assert(dx == 0 && dy == -1);
    rlk_drain_step(1, &dx, &dy);  /* SE */
    assert(dx == 1 && dy == 1);
}

/* build on a simple slope */

static void
test_rlk_drain_build_slope(void) {
    fixture_setup_slope();
    assert(rlk_drain_build(&g_drain, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* Edge cells are outlets */
    assert(g_dir[0] == RLK_DRAIN_OUTLET);              /* (0,0) */
    assert(g_dir[(size_t)(FW - 1)] == RLK_DRAIN_OUTLET); /* (FW-1,0) */
    assert(g_dir[(size_t)(FH - 1) * FW] == RLK_DRAIN_OUTLET); /* (0,FH-1) */

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
test_rlk_drain_build_accumulation(void) {
    fixture_setup_slope();
    assert(rlk_drain_build(&g_drain, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* Every cell has acc >= 1 */
    for (int k = 0; k < FW * FH; k++)
        assert(g_acc[k] >= 1);

    /* Conservation: sum of acc at outlets = total number of cells.
       Outlets are edge cells (dir == RLK_DRAIN_OUTLET). */
    uint32_t total = 0;
    for (int j = 0; j < FH; j++) {
        for (int i = 0; i < FW; i++) {
            if (g_dir[(size_t)j * (size_t)FW + (size_t)i] == RLK_DRAIN_OUTLET)
                total += g_acc[(size_t)j * (size_t)FW + (size_t)i];
        }
    }
    assert(total == (uint32_t)(FW * FH));
}

/* build on a flat grid */

static void
test_rlk_drain_build_flat(void) {
    float flat_buf[FW * FH];
    rlk_grid_t flat = { .lattice = { .w = FW, .h = FH, .cellsize = 1, .origin_x = 0, .origin_y = 0 }, .v = flat_buf };
    for (int k = 0; k < FW * FH; k++)
        flat.v[k] = 0.5f;

    g_drain = (rlk_drain_t){ .lattice = flat.lattice, .filled = g_filled, .dir = g_dir,
                             .acc = g_acc, .order = g_order };
    assert(rlk_drain_build(&g_drain, &flat, -1.0f, g_scratch) == RLK_OK);

    /* All cells processed: every dir is a valid direction or OUTLET */
    for (int k = 0; k < FW * FH; k++)
        assert(g_dir[k] <= RLK_DRAIN_OUTLET);
}

/* build with sea: cells below sea level are outlets */

static void
test_rlk_drain_build_sea(void) {
    fixture_setup_slope();
    /* Sea at elevation 5: cells with i < 5 are ocean */
    assert(rlk_drain_build(&g_drain, &g_elev, 5.0f, g_scratch) == RLK_OK);

    /* Ocean cells are outlets */
    for (int j = 0; j < FH; j++) {
        for (int i = 0; i < 5; i++) {
            assert(g_dir[(size_t)j * (size_t)FW + (size_t)i] == RLK_DRAIN_OUTLET);
        }
    }
    /* Land cells (i >= 5) are not outlets */
    for (int j = 1; j < FH - 1; j++) {
        for (int i = 5; i < FW - 1; i++) {
            assert(g_dir[(size_t)j * (size_t)FW + (size_t)i] != RLK_DRAIN_OUTLET);
        }
    }
}

/* downstream */

static void
test_rlk_drain_downstream(void) {
    fixture_setup_slope();
    assert(rlk_drain_build(&g_drain, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* Interior cell (5,5) flows westward: downstream has i < 5 */
    int ni, nj;
    assert(rlk_drain_downstream(&g_drain, 5, 5, &ni, &nj) == 1);
    assert(ni < 5);  /* west, possibly NW or SW */

    /* Edge cell (0,0) is an outlet: no downstream */
    assert(rlk_drain_downstream(&g_drain, 0, 0, &ni, &nj) == 0);
}

/* fill_elev */

static void
test_rlk_drain_fill_elev(void) {
    fixture_setup_slope();
    assert(rlk_drain_build(&g_drain, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* On a monotonic slope, filled == original (no depressions) */
    for (int j = 0; j < FH; j++) {
        for (int i = 0; i < FW; i++) {
            float filled = rlk_drain_fill_elev(&g_drain, i, j);
            float original = g_elev.v[(size_t)j * (size_t)FW + (size_t)i];
            assert(fabsf(filled - original) < 0.01f);
        }
    }
}

/* lake_depth and is_lake on a pit */

static void
test_rlk_drain_lake(void) {
    /* 5x5 grid with a pit at center: surrounded by higher cells */
    float pit_buf[25];
    rlk_grid_t pit = { .lattice = { .w = 5, .h = 5, .cellsize = 1, .origin_x = 0, .origin_y = 0 }, .v = pit_buf };
    for (int k = 0; k < 25; k++)
        pit.v[k] = 1.0f;
    pit.v[2 * 5 + 2] = 0.0f;  /* pit at (2,2) */

    uint16_t filled[25];
    uint8_t dir[25];
    uint32_t acc[25];
    int order[25];
    uint64_t scratch[25];

    rlk_drain_t drain = { .lattice = pit.lattice, .filled = filled, .dir = dir,
                          .acc = acc, .order = order };
    assert(rlk_drain_build(&drain, &pit, -1.0f, scratch) == RLK_OK);

    /* The pit cell is filled up to the edge level (1.0) */
    float depth = rlk_drain_lake_depth(&drain, &pit, -1.0f, 2, 2);
    assert(depth > 0.9f && depth < 1.1f);
    assert(rlk_drain_is_lake(&drain, &pit, -1.0f, 0.5f, 2, 2) == 1);

    /* A cell on the slope is not a lake */
    assert(rlk_drain_is_lake(&drain, &pit, -1.0f, 0.5f, 0, 0) == 0);
}

/* remove_pits: fill sinks only, without dir/acc/order */

static void
test_rlk_drain_remove_pits(void) {
    /* 5x5 grid with a pit at center: surrounded by higher cells */
    float pit_buf[25];
    rlk_grid_t pit = { .lattice = { .w = 5, .h = 5, .cellsize = 1, .origin_x = 0, .origin_y = 0 }, .v = pit_buf };
    for (int k = 0; k < 25; k++)
        pit.v[k] = 1.0f;
    pit.v[2 * 5 + 2] = 0.0f;  /* pit at (2,2) */

    uint16_t out[25];
    uint64_t scratch[25];
    assert(rlk_drain_remove_pits(out, &pit, -1.0f, scratch) == RLK_OK);

    /* The pit is filled up to the edge level (1.0 -> 256) */
    assert(out[2 * 5 + 2] == rlk_drain_quant(1.0f));
    /* A cell on the rim is unchanged */
    assert(out[0] == rlk_drain_quant(1.0f));
}

/* river_sdf on the slope fixture (cellsize 1: half-width capped at 0.5) */

static void
test_rlk_drain_river_sdf(void) {
    fixture_setup_slope();
    assert(rlk_drain_build(&g_drain, &g_elev, -1.0f, g_scratch) == RLK_OK);

    /* Every interior river cell: the tile centre lies on its own segment,
       dist = 0, half-width = 0.5 (capped by cellsize 1) -> sdf = -0.5.
       This also locks the width cap: cells with different acc give the
       same sdf. */
    int rivers = 0;
    for (int j = 1; j < FH - 1; j++) {
        for (int i = 1; i < FW - 1; i++) {
            const size_t c = (size_t)j * (size_t)FW + (size_t)i;
            if (g_dir[c] == RLK_DRAIN_OUTLET) continue;
            rivers++;
            assert(rlk_drain_river_sdf(&g_drain, i, j, 1) == -0.5f);
        }
    }
    assert(rivers > 0);

    /* acc_min above every accumulation: no network -> RLK_DRAIN_FAR */
    assert(rlk_drain_river_sdf(&g_drain, 5, 5, 1000) == RLK_DRAIN_FAR);

    /* Outside the grid -> RLK_DRAIN_FAR */
    assert(rlk_drain_river_sdf(&g_drain, -1, 0, 1) == RLK_DRAIN_FAR);
    assert(rlk_drain_river_sdf(&g_drain, 0, -1, 1) == RLK_DRAIN_FAR);
    assert(rlk_drain_river_sdf(&g_drain, FW, 0, 1) == RLK_DRAIN_FAR);
    assert(rlk_drain_river_sdf(&g_drain, 0, FH, 1) == RLK_DRAIN_FAR);

    /* Pure: same call, same result */
    assert(rlk_drain_river_sdf(&g_drain, 5, 5, 1)
           == rlk_drain_river_sdf(&g_drain, 5, 5, 1));
}

/* river_sdf on a coarse grid (cellsize 4): network found, finite sdf */

static void
test_rlk_drain_river_sdf_coarse(void) {
    float buf[FW * FH];
    rlk_grid_t elev = { .lattice = { .w = FW, .h = FH, .cellsize = 4,
                                     .origin_x = -32, .origin_y = -32 }, .v = buf };
    for (int j = 0; j < FH; j++)
        for (int i = 0; i < FW; i++)
            buf[(size_t)j * (size_t)FW + (size_t)i] = (float)i;
    rlk_drain_t drain = { .lattice = elev.lattice, .filled = g_filled, .dir = g_dir,
                          .acc = g_acc, .order = g_order };
    assert(rlk_drain_build(&drain, &elev, -1.0f, g_scratch) == RLK_OK);

    /* Interior river cell: its own segment is tested -> finite sdf */
    int found = 0;
    for (int j = 1; j < FH - 1; j++) {
        for (int i = 1; i < FW - 1; i++) {
            const size_t c = (size_t)j * (size_t)FW + (size_t)i;
            if (g_dir[c] == RLK_DRAIN_OUTLET) continue;
            /* tile at the cell centre: (origin + (i+0.5)*cellsize - 0.5) */
            int x = -32 + (int)(((float)i + 0.5f) * 4.0f - 0.5f);
            int y = -32 + (int)(((float)j + 0.5f) * 4.0f - 0.5f);
            float sdf = rlk_drain_river_sdf(&drain, x, y, 1);
            assert(sdf < RLK_DRAIN_FAR);
            found++;
        }
    }
    assert(found > 0);
}

/* acc_mask: 255 where lo <= acc <= hi */

static void
test_rlk_drain_acc_mask(void) {
    fixture_setup_slope();
    assert(rlk_drain_build(&g_drain, &g_elev, -1.0f, g_scratch) == RLK_OK);

    static uint8_t mbuf[FW * FH];
    rlk_mask_t m = { .lattice = g_drain.lattice, .v = mbuf };
    assert(rlk_drain_acc_mask(&m, &g_drain, 1, 1) == RLK_OK);
    int sources = 0;
    for (int k = 0; k < FW * FH; k++) {
        assert(mbuf[k] == (uint8_t)(g_acc[k] == 1 ? 255 : 0));
        if (mbuf[k] == 255) sources++;
    }
    assert(sources > 0);

    /* Geometry mismatch -> RLK_E_INVALID */
    rlk_mask_t bad = { .lattice = { .w = 2, .h = 2, .cellsize = 1,
                                    .origin_x = 0, .origin_y = 0 }, .v = mbuf };
    assert(rlk_drain_acc_mask(&bad, &g_drain, 1, 1) == RLK_E_INVALID);
}

/* build error: elevation outside [0, 256) */

static void
test_rlk_drain_build_range_error(void) {
    float buf[4];
    rlk_grid_t g = { .lattice = { .w = 2, .h = 2, .cellsize = 1, .origin_x = 0, .origin_y = 0 }, .v = buf };
    g.v[0] = 0.0f;
    g.v[1] = 300.0f;  /* >= 256 */
    g.v[2] = 0.0f;
    g.v[3] = 0.0f;

    uint16_t filled[4];
    uint8_t dir[4];
    uint32_t acc[4];
    int order[4];
    uint64_t scratch[4];

    rlk_drain_t drain = { .lattice = g.lattice, .filled = filled, .dir = dir,
                          .acc = acc, .order = order };
    assert(rlk_drain_build(&drain, &g, -1.0f, scratch) == RLK_E_INVALID);

    /* Negative elevations are rejected too (absolute quantization) */
    g.v[1] = -0.5f;
    assert(rlk_drain_build(&drain, &g, -1.0f, scratch) == RLK_E_INVALID);
    assert(rlk_drain_remove_pits(filled, &g, -1.0f, scratch) == RLK_E_INVALID);
}

/* Main */

int
main(void) {
    test_rlk_drain_pod_init();
    test_rlk_drain_quant();
    test_rlk_drain_step();
    test_rlk_drain_build_slope();
    test_rlk_drain_build_accumulation();
    test_rlk_drain_build_flat();
    test_rlk_drain_build_sea();
    test_rlk_drain_downstream();
    test_rlk_drain_fill_elev();
    test_rlk_drain_lake();
    test_rlk_drain_remove_pits();
    test_rlk_drain_river_sdf();
    test_rlk_drain_river_sdf_coarse();
    test_rlk_drain_acc_mask();
    test_rlk_drain_build_range_error();
    return 0;
}
