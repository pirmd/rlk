#include <stddef.h>
#include <string.h>

#include "test.h"
#include "rl/world/tilemap.h"

/* _required_size is cells*sizeof(cell) + ceil(cells/8) mask bytes. */
static void
test_required_size_small(void) {
    /* 8x8 = 64 cells; 64 bits => 8 mask bytes. */
    size_t sz = rlk_tilemap_required_size(8, 8);
    ASSERT(sz == 64 * sizeof(rlk_tilecell_t) + 8);
}

static void
test_required_size_non_multiple(void) {
    /* 9x9 = 81 cells; ceil(81/8) = 11 mask bytes. */
    size_t sz = rlk_tilemap_required_size(9, 9);
    ASSERT(sz == 81 * sizeof(rlk_tilecell_t) + 11);
}

static rlk_tilemap_t g_tm;
static unsigned char g_buf[256 * 256 * sizeof(rlk_tilecell_t) + 256 * 256 / 8 + 1];

static void
test_init_success(void) {
    rlk_err_t e = rlk_tilemap_init(&g_tm, 16, 16, 1.0f, g_buf, sizeof g_buf);
    ASSERT(e == RLK_OK);
    ASSERT(g_tm.width == 16 && g_tm.height == 16);
    ASSERT(g_tm.tile_size == 1.0f);
    ASSERT(g_tm.tiles != NULL && g_tm.occ_mask != NULL);
    /* Zero-initialized: all VOID, not solid. */
    ASSERT(rlk_tilemap_get(&g_tm, 0, 0)->type == RL_TILE_VOID);
    ASSERT(rlk_tilemap_is_solid(&g_tm, 5, 5) == 0);
}

static void
test_init_bufsize_too_small(void) {
    rlk_tilemap_t tm;
    rlk_err_t e = rlk_tilemap_init(&tm, 16, 16, 1.0f, g_buf, 4);
    ASSERT(e == RLK_E_BUFSIZE);
}

static void
test_init_null_buffer(void) {
    rlk_tilemap_t tm;
    rlk_err_t e = rlk_tilemap_init(&tm, 8, 8, 1.0f, NULL, 0);
    ASSERT(e == RLK_E_BUFSIZE);
}

static void
test_init_bad_config(void) {
    rlk_tilemap_t tm;
    ASSERT(rlk_tilemap_init(&tm, 0, 8, 1.0f, g_buf, sizeof g_buf) == RLK_E_CONFIG);
    ASSERT(rlk_tilemap_init(&tm, 8, -1, 1.0f, g_buf, sizeof g_buf) == RLK_E_CONFIG);
    ASSERT(rlk_tilemap_init(&tm, 8, 8, 0.0f, g_buf, sizeof g_buf) == RLK_E_CONFIG);
}

static void
test_get_set_tile(void) {
    rlk_tilemap_init(&g_tm, 8, 8, 1.0f, g_buf, sizeof g_buf);
    rlk_tilecell_t *c = rlk_tilemap_get(&g_tm, 3, 4);
    ASSERT(c != NULL);
    c->type = RL_TILE_GRASS;
    c->biome = 2;
    ASSERT(rlk_tilemap_get(&g_tm, 3, 4)->type == RL_TILE_GRASS);
    ASSERT(rlk_tilemap_get(&g_tm, 3, 4)->biome == 2);
    ASSERT(rlk_tilemap_get(&g_tm, 0, 0)->type == RL_TILE_VOID);
}

static void
test_get_out_of_bounds(void) {
    rlk_tilemap_init(&g_tm, 8, 8, 1.0f, g_buf, sizeof g_buf);
    ASSERT(rlk_tilemap_get(&g_tm, 8, 0) == NULL);
    ASSERT(rlk_tilemap_get(&g_tm, -1, 0) == NULL);
}

static void
test_occ_mask_set_get(void) {
    rlk_tilemap_init(&g_tm, 16, 16, 1.0f, g_buf, sizeof g_buf);
    ASSERT(rlk_tilemap_is_solid(&g_tm, 7, 3) == 0);
    ASSERT(rlk_tilemap_set_occ(&g_tm, 7, 3, 1) == RLK_OK);
    ASSERT(rlk_tilemap_is_solid(&g_tm, 7, 3) == 1);
    ASSERT(rlk_tilemap_set_occ(&g_tm, 7, 3, 0) == RLK_OK);
    ASSERT(rlk_tilemap_is_solid(&g_tm, 7, 3) == 0);
    /* out of bounds => solid */
    ASSERT(rlk_tilemap_is_solid(&g_tm, 99, 99) == 1);
    ASSERT(rlk_tilemap_set_occ(&g_tm, 99, 99, 1) == RLK_E_RANGE);
}

static void
test_occ_mask_bit_packing(void) {
    /* Set every cell solid, then verify each bit reads back. */
    rlk_tilemap_init(&g_tm, 16, 16, 1.0f, g_buf, sizeof g_buf);
    for (int32_t y = 0; y < 16; y++) {
        for (int32_t x = 0; x < 16; x++) {
            ASSERT(rlk_tilemap_set_occ(&g_tm, x, y, 1) == RLK_OK);
        }
    }
    for (int32_t y = 0; y < 16; y++) {
        for (int32_t x = 0; x < 16; x++) {
            ASSERT(rlk_tilemap_is_solid(&g_tm, x, y) == 1);
        }
    }
}

static void
test_is_solid_circle(void) {
    /* 16x16, tile_size=1.0, mark (4,4) solid. Circle overlapping (4.5,4.5). */
    rlk_tilemap_init(&g_tm, 16, 16, 1.0f, g_buf, sizeof g_buf);
    rlk_tilemap_set_occ(&g_tm, 4, 4, 1);
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, 4.5f, 4.5f, 0.2f) == 1);
    /* Circle far from any solid cell. */
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, 10.5f, 10.5f, 0.2f) == 0);
    /* Circle near but not overlapping. */
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, 4.5f, 6.5f, 0.3f) == 0);
    /* Out of bounds => solid. */
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, -1.0f, -1.0f, 0.2f) == 1);
}

int
main(void) {
    test_required_size_small();
    test_required_size_non_multiple();
    test_init_success();
    test_init_bufsize_too_small();
    test_init_null_buffer();
    test_init_bad_config();
    test_get_set_tile();
    test_get_out_of_bounds();
    test_occ_mask_set_get();
    test_occ_mask_bit_packing();
    test_is_solid_circle();
    return 0;
}
