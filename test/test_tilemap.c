#include <stddef.h>
#include <string.h>

#include "test.h"
#include "rl/world/tilemap.h"

/* Local palette: rlk tiles are opaque, the game owns its values. */
enum { TILE_VOID = 0, TILE_FLOOR = 1, TILE_GRASS = 2, TILE_WATER = 5, TILE_STONE = 4 };

/* _required_size is cells*sizeof(cell) + ceil(cells/8) mask bytes. */
static void
test_required_size_small(void) {
    size_t sz = rlk_tilemap_required_size(8, 8);
    ASSERT(sz == 64 * sizeof(rlk_tilecell_t) + 8);
}

static void
test_required_size_non_multiple(void) {
    size_t sz = rlk_tilemap_required_size(9, 9);
    ASSERT(sz == 81 * sizeof(rlk_tilecell_t) + 11);
}

/* Invalid dimensions return 0 (no assert, release-safe). */
static void
test_required_size_invalid(void) {
    ASSERT(rlk_tilemap_required_size(0, 8) == 0);
    ASSERT(rlk_tilemap_required_size(-1, 8) == 0);
    ASSERT(rlk_tilemap_required_size(8, 0) == 0);
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
    ASSERT(rlk_tilemap_get(&g_tm, 0, 0)->type == RLK_TILE_VOID);
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
    c->type = TILE_GRASS;
    c->biome = 2;
    ASSERT(rlk_tilemap_get(&g_tm, 3, 4)->type == TILE_GRASS);
    ASSERT(rlk_tilemap_get(&g_tm, 3, 4)->biome == 2);
    ASSERT(rlk_tilemap_get(&g_tm, 0, 0)->type == RLK_TILE_VOID);
}

static void
test_get_const(void) {
    rlk_tilemap_init(&g_tm, 8, 8, 1.0f, g_buf, sizeof g_buf);
    rlk_tilemap_get(&g_tm, 2, 2)->type = TILE_FLOOR;
    const rlk_tilemap_t *const_tm = &g_tm;
    const rlk_tilecell_t *c = rlk_tilemap_get_c(const_tm, 2, 2);
    ASSERT(c != NULL && c->type == TILE_FLOOR);
    ASSERT(rlk_tilemap_get_c(const_tm, 8, 0) == NULL);
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
    /* set_occ mirrors into the cell flags. */
    ASSERT((rlk_tilemap_get(&g_tm, 7, 3)->flags & RLK_FLAG_SOLID) != 0);
    ASSERT(rlk_tilemap_set_occ(&g_tm, 7, 3, 0) == RLK_OK);
    ASSERT(rlk_tilemap_is_solid(&g_tm, 7, 3) == 0);
    ASSERT((rlk_tilemap_get(&g_tm, 7, 3)->flags & RLK_FLAG_SOLID) == 0);
    ASSERT(rlk_tilemap_is_solid(&g_tm, 99, 99) == 1);
    ASSERT(rlk_tilemap_set_occ(&g_tm, 99, 99, 1) == RLK_E_RANGE);
}

static void
test_occ_mask_bit_packing(void) {
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
    rlk_tilemap_init(&g_tm, 16, 16, 1.0f, g_buf, sizeof g_buf);
    rlk_tilemap_set_occ(&g_tm, 4, 4, 1);
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, 4.5f, 4.5f, 0.2f) == 1);
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, 10.5f, 10.5f, 0.2f) == 0);
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, 4.5f, 6.5f, 0.3f) == 0);
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, -1.0f, -1.0f, 0.2f) == 1);
}

/* floorf ensures the bounding box reaches into negative (out-of-bounds)
 * tiles when the circle crosses the lower/left map edge. Without floorf,
 * negative coordinates truncate toward 0 and the out-of-bounds tile is
 * missed; since out-of-bounds is treated as solid, that would wrongly
 * return 0. Fresh map: no in-bounds solid tiles, so the hit must come from
 * the edge crossing. */
static void
test_is_solid_circle_negative_origin(void) {
    rlk_tilemap_init(&g_tm, 16, 16, 1.0f, g_buf, sizeof g_buf);
    /* Circle crosses the low-left edge into out-of-bounds => solid. */
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, 0.5f, 0.5f, 0.6f) == 1);
    /* Same radius fully inside the map finds no solid tile. */
    ASSERT(rlk_tilemap_is_solid_circle(&g_tm, 5.5f, 5.5f, 0.6f) == 0);
}

int
main(void) {
    test_required_size_small();
    test_required_size_non_multiple();
    test_required_size_invalid();
    test_init_success();
    test_init_bufsize_too_small();
    test_init_null_buffer();
    test_init_bad_config();
    test_get_set_tile();
    test_get_const();
    test_get_out_of_bounds();
    test_occ_mask_set_get();
    test_occ_mask_bit_packing();
    test_is_solid_circle();
    test_is_solid_circle_negative_origin();
    return 0;
}
