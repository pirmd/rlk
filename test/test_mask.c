#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "mask.h"

/* Fixture: 8x6 cells of 2 tiles, origin (10,20), values 0..47. */

#define MW 8
#define MH 6

static float g_buf[MW * MH];
static uint8_t m_buf[MW * MH];
static uint8_t m2_buf[MW * MH];
static rlk_grid_t g_grid;
static rlk_mask_t g_mask;
static rlk_mask_t g_mask2;

static void
fixture(void) {
    g_grid = (rlk_grid_t){ .lattice = { .w = MW, .h = MH, .cellsize = 2,
                                        .origin_x = 10, .origin_y = 20 }, .v = g_buf };
    g_mask = (rlk_mask_t){ .lattice = g_grid.lattice, .v = m_buf };
    g_mask2 = (rlk_mask_t){ .lattice = g_grid.lattice, .v = m2_buf };
    for (int j = 0; j < MH; j++)
        for (int i = 0; i < MW; i++)
            g_buf[(size_t)j * (size_t)MW + (size_t)i] = (float)(i + j * MW);
}

/* weight / allows: world coords, 0 outside the mask. */

static void
test_weight_allows(void) {
    fixture();
    memset(m_buf, 0, sizeof(m_buf));
    m_buf[0] = 200;             /* cell (0,0) = world (10,20) */
    m_buf[MW * MH - 1] = 255;  /* cell (7,5) = world (24,30) */
    assert(rlk_mask_weight(&g_mask, 10, 20) == 200);
    assert(rlk_mask_weight(&g_mask, 24, 30) == 255);
    assert(rlk_mask_weight(&g_mask, 11, 21) == 200);  /* same cell */
    assert(rlk_mask_weight(&g_mask, 25, 31) == 255);  /* cell (7,5) */
    assert(rlk_mask_weight(&g_mask, 26, 20) == 0);    /* outside */
    assert(rlk_mask_weight(&g_mask, 9, 20) == 0);
    assert(rlk_mask_weight(&g_mask, 10, 32) == 0);
    assert(rlk_mask_allows(&g_mask, 10, 20) == 1);
    assert(rlk_mask_allows(&g_mask, 12, 20) == 0);    /* weight 0 */
    assert(rlk_mask_allows(&g_mask, 0, 0) == 0);      /* outside */
}

/* init_like: copies the grid geometry, sets the buffer. */

static void
test_init_like(void) {
    fixture();
    uint8_t buf[MW * MH];
    rlk_mask_t m;
    rlk_mask_init_like(&m, &g_grid, buf);
    assert(m.v == buf);
    assert(rlk_lattice_same(&m.lattice, &g_grid.lattice));
}

/* threshold: 255 in [lo, hi], else 0. */

static void
test_threshold(void) {
    fixture();
    assert(rlk_mask_threshold(&g_mask, &g_grid, 10.0f, 20.0f) == RLK_OK);
    int count = 0;
    for (int k = 0; k < MW * MH; k++) {
        assert(m_buf[k] == (uint8_t)((k >= 10 && k <= 20) ? 255 : 0));
        if (m_buf[k] == 255) count++;
    }
    assert(count == 11);
}

/* threshold: geometry mismatch -> RLK_E_INVALID. */

static void
test_threshold_geometry_mismatch(void) {
    fixture();
    uint8_t buf[MW * MH];
    rlk_mask_t bad = { .lattice = { .w = MW, .h = MH, .cellsize = 1,
                                    .origin_x = 0, .origin_y = 0 }, .v = buf };
    assert(rlk_mask_threshold(&bad, &g_grid, 0.0f, 1.0f) == RLK_E_INVALID);
}

/* ramp: linear 0 -> 255, inverted ramp, degenerate v0 == v1. */

static void
test_ramp(void) {
    fixture();  /* values 0..47 */
    assert(rlk_mask_ramp(&g_mask, &g_grid, 0.0f, 4.0f) == RLK_OK);
    assert(m_buf[0] == 0);    /* v=0 -> 0 */
    assert(m_buf[1] == 64);   /* v=1 -> 63.75 -> 64 */
    assert(m_buf[2] == 128);  /* v=2 -> 127.5 -> 128 */
    assert(m_buf[3] == 191);  /* v=3 -> 191.25 -> 191 */
    assert(m_buf[4] == 255);  /* v=4 -> 255 */
    assert(m_buf[47] == 255); /* clamped */

    /* Inverted ramp (v1 < v0). */
    assert(rlk_mask_ramp(&g_mask, &g_grid, 4.0f, 0.0f) == RLK_OK);
    assert(m_buf[0] == 255);
    assert(m_buf[2] == 128);
    assert(m_buf[4] == 0);

    /* Degenerate ramp (v0 == v1): hard threshold. */
    assert(rlk_mask_ramp(&g_mask, &g_grid, 4.0f, 4.0f) == RLK_OK);
    assert(m_buf[3] == 0);
    assert(m_buf[4] == 255);
    assert(m_buf[47] == 255);
}

/* and: dst[c] = min(dst[c], src[c]). */

static void
test_and(void) {
    fixture();
    for (int k = 0; k < MW * MH; k++) {
        m_buf[k] = (uint8_t)(k * 5);     /* 0..235 */
        m2_buf[k] = (uint8_t)(200 - k);  /* 200..156 */
    }
    assert(rlk_mask_and(&g_mask, &g_mask2) == RLK_OK);
    for (int k = 0; k < MW * MH; k++) {
        uint8_t expect = (uint8_t)((k * 5 < 200 - k) ? k * 5 : 200 - k);
        assert(m_buf[k] == expect);
    }
    /* Geometry mismatch. */
    rlk_mask_t bad = { .lattice = { .w = 2, .h = 2, .cellsize = 1,
                                    .origin_x = 0, .origin_y = 0 }, .v = m2_buf };
    assert(rlk_mask_and(&bad, &g_mask) == RLK_E_INVALID);
}

/* from_tilemap: full recompute, out-of-table type, incremental region,
   geometry mismatch. */

static void
test_from_tilemap(void) {
    /* 4x3 tilemap at world origin (5,7), types cycle 0,1,2. */
    rlk_tile_t tiles[12];
    rlk_tilemap_t tm = { .w = 4, .h = 3, .origin_x = 5, .origin_y = 7, .tiles = tiles };
    for (int k = 0; k < 12; k++)
        tiles[k] = (rlk_tile_t){ .type = (uint8_t)(k % 3) };
    static const uint8_t flags[3] = {
        RLK_TILE_WALKABLE, 0, RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    };
    rlk_tile_props_t props = { .flags = flags, .ntypes = 3 };

    uint8_t buf[12];
    rlk_mask_t m = { .lattice = rlk_tilemap_lattice(&tm), .v = buf };
    assert(rlk_mask_from_tilemap(&m, &tm, &props, RLK_TILE_WALKABLE, NULL) == RLK_OK);
    for (int k = 0; k < 12; k++)
        assert(buf[k] == (uint8_t)((k % 3 == 1) ? 0 : 255));

    /* A type outside the table has no flag. */
    tiles[0].type = 9;
    assert(rlk_mask_from_tilemap(&m, &tm, &props, RLK_TILE_WALKABLE, NULL) == RLK_OK);
    assert(buf[0] == 0);
    tiles[0].type = 0;

    /* Incremental update: region covers only the first row. */
    memset(buf, 0xAA, sizeof(buf));
    rlk_rect_t region = { 5, 7, 4, 1 };  /* first row, world coords */
    assert(rlk_mask_from_tilemap(&m, &tm, &props, RLK_TILE_WALKABLE, &region) == RLK_OK);
    for (int k = 0; k < 4; k++)
        assert(buf[k] == (uint8_t)((k % 3 == 1) ? 0 : 255));
    for (int k = 4; k < 12; k++)
        assert(buf[k] == 0xAA);  /* untouched */

    /* Geometry mismatch (cellsize 2) -> RLK_E_INVALID. */
    uint8_t buf2[12];
    rlk_mask_t m2 = { .lattice = { .w = 4, .h = 3, .cellsize = 2,
                                   .origin_x = 5, .origin_y = 7 }, .v = buf2 };
    assert(rlk_mask_from_tilemap(&m2, &tm, &props, RLK_TILE_WALKABLE, NULL) == RLK_E_INVALID);
}

/* Main */

int
main(void) {
    test_weight_allows();
    test_init_like();
    test_threshold();
    test_threshold_geometry_mismatch();
    test_ramp();
    test_and();
    test_from_tilemap();
    return 0;
}
