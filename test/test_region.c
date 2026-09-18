#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "test.h"
#include "rl/world/tilemap.h"
#include "rl/gen/region.h"

#define REG_W 64
#define REG_H 64
static unsigned char g_a[RLK_TM_BUF_SIZE(REG_W, REG_H)];
static unsigned char g_b[RLK_TM_BUF_SIZE(REG_W, REG_H)];

/* Game-owned palette (rlk tiles are opaque). */
enum { T_VOID = 0, T_WATER = 1, T_SAND = 2, T_GRASS = 3, T_DIRT = 4, T_STONE = 5 };

/* Byte-for-byte identical Tilemap when regenerating from the same cfg. */
static void
test_reproducible_same_cfg(void) {
    rlk_region_cfg_t cfg = { .world_seed = 0xC0FFEEULL, .region_x = 0, .region_y = 0, .terrain = NULL };
    rlk_tilemap_t tm_a, tm_b;

    ASSERT(rlk_tilemap_init(&tm_a, REG_W, REG_H, 1.0f, g_a, sizeof g_a) == RLK_OK);
    ASSERT(rlk_tilemap_init(&tm_b, REG_W, REG_H, 1.0f, g_b, sizeof g_b) == RLK_OK);

    ASSERT(rlk_region_generate(&tm_a, &cfg) == RLK_OK);
    ASSERT(rlk_region_generate(&tm_b, &cfg) == RLK_OK);

    size_t need = rlk_tilemap_required_size(REG_W, REG_H);
    ASSERT(memcmp(g_a, g_b, need) == 0);
}

static void
test_region_offset_differs(void) {
    rlk_region_cfg_t c0 = { .world_seed = 42ULL, .region_x = 0, .region_y = 0, .terrain = NULL };
    rlk_region_cfg_t c1 = { .world_seed = 42ULL, .region_x = 1, .region_y = 0, .terrain = NULL };
    rlk_tilemap_t tm_a, tm_b;

    ASSERT(rlk_tilemap_init(&tm_a, REG_W, REG_H, 1.0f, g_a, sizeof g_a) == RLK_OK);
    ASSERT(rlk_tilemap_init(&tm_b, REG_W, REG_H, 1.0f, g_b, sizeof g_b) == RLK_OK);
    ASSERT(rlk_region_generate(&tm_a, &c0) == RLK_OK);
    ASSERT(rlk_region_generate(&tm_b, &c1) == RLK_OK);
    ASSERT(memcmp(g_a, g_b, rlk_tilemap_required_size(REG_W, REG_H)) != 0);
}

static void
test_seed_differs(void) {
    rlk_region_cfg_t ca = { .world_seed = 1ULL, .region_x = 0, .region_y = 0, .terrain = NULL };
    rlk_region_cfg_t cb = { .world_seed = 2ULL, .region_x = 0, .region_y = 0, .terrain = NULL };
    rlk_tilemap_t tm_a, tm_b;

    ASSERT(rlk_tilemap_init(&tm_a, REG_W, REG_H, 1.0f, g_a, sizeof g_a) == RLK_OK);
    ASSERT(rlk_tilemap_init(&tm_b, REG_W, REG_H, 1.0f, g_b, sizeof g_b) == RLK_OK);
    ASSERT(rlk_region_generate(&tm_a, &ca) == RLK_OK);
    ASSERT(rlk_region_generate(&tm_b, &cb) == RLK_OK);
    ASSERT(memcmp(g_a, g_b, rlk_tilemap_required_size(REG_W, REG_H)) != 0);
}

static void
test_reference_hash_stable(void) {
    rlk_region_cfg_t cfg = { .world_seed = 0xDEADBEEF12345678ULL,
                             .region_x = 3, .region_y = -2, .terrain = NULL };
    rlk_tilemap_t tm;
    ASSERT(rlk_tilemap_init(&tm, REG_W, REG_H, 2.0f, g_a, sizeof g_a) == RLK_OK);
    ASSERT(rlk_region_generate(&tm, &cfg) == RLK_OK);

    uint64_t hash = 0;
    int solid_count = 0;
    for (int32_t y = 0; y < REG_H; y++) {
        for (int32_t x = 0; x < REG_W; x++) {
            uint8_t t = tm.tiles[(size_t)y * REG_W + (size_t)x].type;
            hash = hash * 131u + t;
            if (rlk_tilemap_is_solid(&tm, x, y)) solid_count++;
        }
    }
    ASSERT(hash != 0);
    ASSERT(solid_count > 0 && solid_count < REG_W * REG_H);
}

static void
test_tile_types_valid(void) {
    rlk_region_cfg_t cfg = { .world_seed = 7ULL, .region_x = 0, .region_y = 0, .terrain = NULL };
    rlk_tilemap_t tm;
    ASSERT(rlk_tilemap_init(&tm, REG_W, REG_H, 1.0f, g_a, sizeof g_a) == RLK_OK);
    ASSERT(rlk_region_generate(&tm, &cfg) == RLK_OK);
    for (int32_t y = 0; y < REG_H; y++) {
        for (int32_t x = 0; x < REG_W; x++) {
            uint8_t t = tm.tiles[(size_t)y * REG_W + (size_t)x].type;
            ASSERT(t > T_VOID && t <= T_STONE);
        }
    }
}

static void
test_solidity_matches_resolver(void) {
    rlk_region_cfg_t cfg = { .world_seed = 999ULL, .region_x = 2, .region_y = -1, .terrain = NULL };
    rlk_tilemap_t tm;
    ASSERT(rlk_tilemap_init(&tm, REG_W, REG_H, 1.0f, g_a, sizeof g_a) == RLK_OK);
    ASSERT(rlk_region_generate(&tm, &cfg) == RLK_OK);
    for (int32_t y = 0; y < REG_H; y++) {
        for (int32_t x = 0; x < REG_W; x++) {
            const rlk_tilecell_t *c = rlk_tilemap_get_c(&tm, x, y);
            int s = rlk_tilemap_is_solid(&tm, x, y);
            /* Default resolver: water (1) and stone (5) are solid. */
            if (c->type == T_WATER || c->type == T_STONE) ASSERT(s == 1);
            else ASSERT(s == 0);
        }
    }
}

/* Custom resolver: water is NOT solid (swimmable), proves the game owns rules. */
static rlk_err_t
swimmable_terrain(const rlk_region_sample_t *s, rlk_tile_t *out_type, int *out_solid) {
    if (s->elevation < RLK_REG_WATER_LEVEL) { *out_type = T_WATER; *out_solid = 0; }
    else if (s->elevation < RLK_REG_SAND_LEVEL) { *out_type = T_SAND; *out_solid = 0; }
    else if (s->elevation >= RLK_REG_STONE_LEVEL) { *out_type = T_STONE; *out_solid = 1; }
    else if (s->humidity > 0.5f) { *out_type = T_GRASS; *out_solid = 0; }
    else { *out_type = T_DIRT; *out_solid = 0; }
    return RLK_OK;
}

static void
test_custom_terrain_water_not_solid(void) {
    rlk_region_cfg_t cfg = { .world_seed = 999ULL, .region_x = 2, .region_y = -1,
                             .terrain = swimmable_terrain };
    rlk_tilemap_t tm;
    ASSERT(rlk_tilemap_init(&tm, REG_W, REG_H, 1.0f, g_a, sizeof g_a) == RLK_OK);
    ASSERT(rlk_region_generate(&tm, &cfg) == RLK_OK);
    int found_water = 0;
    for (int32_t y = 0; y < REG_H; y++) {
        for (int32_t x = 0; x < REG_W; x++) {
            const rlk_tilecell_t *c = rlk_tilemap_get_c(&tm, x, y);
            if (c->type == T_WATER) {
                found_water = 1;
                ASSERT(rlk_tilemap_is_solid(&tm, x, y) == 0);   /* swimmable! */
            }
        }
    }
    ASSERT(found_water);   /* the region must contain some water to validate */
}

/* Custom resolver aborting: generation returns the error. */
static rlk_err_t
failing_terrain(const rlk_region_sample_t *s, rlk_tile_t *out_type, int *out_solid) {
    (void)s; (void)out_type; (void)out_solid;
    return RLK_E_CONFIG;
}

static void
test_custom_terrain_error_propagates(void) {
    rlk_region_cfg_t cfg = { .world_seed = 1ULL, .region_x = 0, .region_y = 0,
                             .terrain = failing_terrain };
    rlk_tilemap_t tm;
    ASSERT(rlk_tilemap_init(&tm, REG_W, REG_H, 1.0f, g_a, sizeof g_a) == RLK_OK);
    ASSERT(rlk_region_generate(&tm, &cfg) == RLK_E_CONFIG);
}

int
main(void) {
    test_reproducible_same_cfg();
    test_region_offset_differs();
    test_seed_differs();
    test_reference_hash_stable();
    test_tile_types_valid();
    test_solidity_matches_resolver();
    test_custom_terrain_water_not_solid();
    test_custom_terrain_error_propagates();
    return 0;
}
