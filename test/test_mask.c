#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "grid.h"
#include "mask.h"

/* Game-defined flag bits for the tests (rlk reserves none). */
#define F_WALKABLE    1u
#define F_TRANSPARENT 2u

/* Fixture: 8x6 cells of 2 tiles, origin (10,20), values 0..47. */

#define MW 8
#define MH 6

static float g_buf[MW * MH];
static uint8_t m_buf[MW * MH];
static rlk_grid_t g_grid;
static rlk_mask_t g_mask;

static void
fixture(void) {
    g_grid = (rlk_grid_t){ .lattice = { .w = MW, .h = MH, .cellsize = 2,
                                        .origin_x = 10, .origin_y = 20 }, .values = g_buf };
    g_mask = (rlk_mask_t){ .lattice = g_grid.lattice, .weights = m_buf };
    for (int j = 0; j < MH; j++)
        for (int i = 0; i < MW; i++)
            g_buf[(size_t)j * (size_t)MW + (size_t)i] = (float)(i + j * MW);
}

/* sample / allows: world coords, 0 outside the mask. */

static void
test_sample_allows(void) {
    fixture();
    memset(m_buf, 0, sizeof(m_buf));
    m_buf[0] = 200;             /* cell (0,0) = world (10,20) */
    m_buf[MW * MH - 1] = 255;  /* cell (7,5) = world (24,30) */
    assert(rlk_mask_get(&g_mask, 10, 20) == 200);
    assert(rlk_mask_get(&g_mask, 24, 30) == 255);
    assert(rlk_mask_get(&g_mask, 11, 21) == 200);  /* same cell */
    assert(rlk_mask_get(&g_mask, 25, 31) == 255);  /* cell (7,5) */
    assert(rlk_mask_get(&g_mask, 26, 20) == 0);    /* outside */
    assert(rlk_mask_get(&g_mask, 9, 20) == 0);
    assert(rlk_mask_get(&g_mask, 10, 32) == 0);
    assert(rlk_mask_allows(&g_mask, 10, 20) == 1);
    assert(rlk_mask_allows(&g_mask, 12, 20) == 0);    /* weight 0 */
    assert(rlk_mask_allows(&g_mask, 0, 0) == 0);      /* outside */
}

/* from_tilemap: full recompute, out-of-table type, incremental dirty rect,
   geometry mismatch. */

static void
test_from_tilemap(void) {
    /* 4x3 tilemap at world origin (5,7), types cycle 0,1,2. */
    rlk_tile_t tiles[12];
    rlk_tilemap_t tm = { .w = 4, .h = 3, .origin_x = 5, .origin_y = 7, .tiles = tiles };
    for (int k = 0; k < 12; k++)
        tiles[k] = (rlk_tile_t){ .type = (uint8_t)(k % 3) };
    static const uint8_t flags[3] = {
        F_WALKABLE, 0, F_WALKABLE | F_TRANSPARENT,
    };
    rlk_tile_props_t props = { .flags = flags, .ntypes = 3 };

    uint8_t buf[12];
    rlk_mask_t m = { .lattice = rlk_tilemap_lattice(&tm), .weights = buf };
    assert(rlk_mask_from_tilemap(&m, &tm, &props, F_WALKABLE, NULL) == RLK_OK);
    for (int k = 0; k < 12; k++)
        assert(buf[k] == (uint8_t)((k % 3 == 1) ? 0 : 255));

    /* A type outside the table has no flag. */
    tiles[0].type = 9;
    assert(rlk_mask_from_tilemap(&m, &tm, &props, F_WALKABLE, NULL) == RLK_OK);
    assert(buf[0] == 0);
    tiles[0].type = 0;

    /* Incremental update: dirty covers only the first row. */
    memset(buf, 0xAA, sizeof(buf));
    rlk_rect_t dirty = { 5, 7, 4, 1 };  /* first row, world coords */
    assert(rlk_mask_from_tilemap(&m, &tm, &props, F_WALKABLE, &dirty) == RLK_OK);
    for (int k = 0; k < 4; k++)
        assert(buf[k] == (uint8_t)((k % 3 == 1) ? 0 : 255));
    for (int k = 4; k < 12; k++)
        assert(buf[k] == 0xAA);  /* untouched */

    /* Geometry mismatch (cellsize 2) -> RLK_E_INVALID. */
    uint8_t buf2[12];
    rlk_mask_t m2 = { .lattice = { .w = 4, .h = 3, .cellsize = 2,
                                   .origin_x = 5, .origin_y = 7 }, .weights = buf2 };
    assert(rlk_mask_from_tilemap(&m2, &tm, &props, F_WALKABLE, NULL) == RLK_E_INVALID);
}

/* Main */

int
main(void) {
    test_sample_allows();
    test_from_tilemap();
    return 0;
}
