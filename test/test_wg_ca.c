#include <assert.h>
#include <string.h>
#include "wg_ca.h"

#define FIXTURE_MAP_W 32
#define FIXTURE_MAP_H 32
#define FIXTURE_FILL_PROB 0.45f
#define FIXTURE_ITERATIONS 5

#define FIXTURE_GRID_SIZE (FIXTURE_MAP_W * FIXTURE_MAP_H)

static uint8_t g_grid[FIXTURE_GRID_SIZE];
static int g_region_ids[FIXTURE_MAP_W * FIXTURE_MAP_H];
static rlk_wg_ca_t g_ca;
static const rlk_wg_ca_wallfloor_t g_wf = { .wall_type = 1, .floor_type = 0 };

static uint8_t g_tile_buf[FIXTURE_MAP_W * FIXTURE_MAP_H * sizeof(rlk_tile_t)];
static rlk_tilemap_t g_tilemap;

static void
fixture_setup(void) {
    g_ca = (rlk_wg_ca_t){
        .width = FIXTURE_MAP_W,
        .height = FIXTURE_MAP_H,
        .seed = 42,
        .fill_prob = FIXTURE_FILL_PROB,
        .iterations = FIXTURE_ITERATIONS,
        .connect = 1,
        .write = rlk_wg_ca_write_wallfloor,
        .write_data = &g_wf,
        .grid = g_grid,
        .region_ids = g_region_ids,
    };
    rlk_wg_ca_init(&g_ca);

    rlk_tilemap_init(&g_tilemap, FIXTURE_MAP_W, FIXTURE_MAP_H, g_tile_buf, sizeof(g_tile_buf));
}

/* --- INIT (config only) -------------------------------------------------- */

static void
test_init_valid(void) {
    fixture_setup();

    assert(g_ca.grid == g_grid);
    assert(g_ca.region_ids == g_region_ids);
    assert(g_ca.width == FIXTURE_MAP_W);
    assert(g_ca.height == FIXTURE_MAP_H);
    assert(g_ca.seed == 42);
    assert(g_ca.fill_prob == FIXTURE_FILL_PROB);
    assert(g_ca.write == rlk_wg_ca_write_wallfloor);
    assert(g_ca.write_data == &g_wf);
    /* Defaults resolved from zero. */
    assert(g_ca.birth_low == 4);
    assert(g_ca.birth_high == 5);
    assert(g_ca.survive_low == 4);
    assert(g_ca.survive_high == 8);
    assert(g_ca.iterations == FIXTURE_ITERATIONS);
}

static void
test_init_default(void) {
    rlk_wg_ca_t ca;
    uint8_t grid[16 * 16];
    int region_ids[16 * 16];

    /* All zero except required fields — defaults should be resolved. */
    static const rlk_wg_ca_wallfloor_t wf = { .wall_type = 1, .floor_type = 0 };
    ca = (rlk_wg_ca_t){
        .width = 16,
        .height = 16,
        .seed = 123,
        .write = rlk_wg_ca_write_wallfloor,
        .write_data = &wf,
        .grid = grid,
        .region_ids = region_ids,
    };
    rlk_wg_ca_init(&ca);

    assert(ca.fill_prob == 0.45f);
    assert(ca.iterations == 5);
    assert(ca.birth_low == 4);
    assert(ca.birth_high == 5);
    assert(ca.survive_low == 4);
    assert(ca.survive_high == 8);
    assert(ca.connect == 0);  /* not resolved: 0 means skip connectivity */
    assert(ca.seed == 123);
}

/* --- FILL (generate + render) -------------------------------------------- */

static void
test_fill_basic(void) {
    fixture_setup();

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    int has_wall = 0, has_floor = 0;
    for (int y = 0; y < g_tilemap.height; y++) {
        for (int x = 0; x < g_tilemap.width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            assert(tile != NULL);
            if (tile->type == 1) has_wall = 1;
            if (tile->type == 0) has_floor = 1;
        }
    }

    assert(has_wall == 1);
    assert(has_floor == 1);
}

static void
test_fill_grid(void) {
    fixture_setup();

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    /* After fill: grid has walls+floors, iterations ran. */
    int floor_count = 0;
    int wall_count = 0;
    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        if (g_ca.grid[i] == 1) floor_count++;
        else if (g_ca.grid[i] == 0) wall_count++;
    }
    assert(floor_count > 0);
    assert(wall_count > 0);
    assert(g_ca.cur_iter == FIXTURE_ITERATIONS);
}

static void
test_fill_connected(void) {
    fixture_setup();

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    assert(g_ca.region_count <= 1);
}

static void
test_fill_deterministic(void) {
    uint8_t grid1[FIXTURE_GRID_SIZE];
    int region_ids1[FIXTURE_MAP_W * FIXTURE_MAP_H];
    rlk_wg_ca_t ca1 = {
        .width = FIXTURE_MAP_W,
        .height = FIXTURE_MAP_H,
        .seed = 42,
        .fill_prob = FIXTURE_FILL_PROB,
        .iterations = FIXTURE_ITERATIONS,
        .write = rlk_wg_ca_write_wallfloor,
        .write_data = &g_wf,
        .grid = grid1,
        .region_ids = region_ids1,
    };
    rlk_wg_ca_init(&ca1);
    rlk_wg_ca_fill(&ca1, 0, 0, &g_tilemap);

    uint8_t grid2[FIXTURE_GRID_SIZE];
    int region_ids2[FIXTURE_MAP_W * FIXTURE_MAP_H];
    rlk_wg_ca_t ca2 = {
        .width = FIXTURE_MAP_W,
        .height = FIXTURE_MAP_H,
        .seed = 42,
        .fill_prob = FIXTURE_FILL_PROB,
        .iterations = FIXTURE_ITERATIONS,
        .write = rlk_wg_ca_write_wallfloor,
        .write_data = &g_wf,
        .grid = grid2,
        .region_ids = region_ids2,
    };
    rlk_wg_ca_init(&ca2);
    rlk_wg_ca_fill(&ca2, 0, 0, &g_tilemap);

    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        assert(grid1[i] == grid2[i]);
    }
    assert(ca1.region_count == ca2.region_count);
}

static void
test_fill_consistency(void) {
    fixture_setup();

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    for (int y = 0; y < g_ca.height; y++) {
        for (int x = 0; x < g_ca.width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            uint8_t cell = g_ca.grid[(size_t)y * (size_t)g_ca.width + (size_t)x];
            uint8_t expected_type = (cell == 1) ? 0 : 1;
            assert(tile->type == expected_type);
        }
    }
}

static void
test_fill_offset(void) {
    fixture_setup();

    /* Sentinel: mark all tiles as type 99. */
    for (int y = 0; y < g_tilemap.height; y++)
        for (int x = 0; x < g_tilemap.width; x++)
            rlk_tilemap_get(&g_tilemap, x, y)->type = 99;

    /* Fill at offset (5, 5). CA grid is 32x32, tilemap is 32x32.
       Tiles (0,0)-(4,31) and (0,0)-(31,4) should stay 99.
       Tiles (5,5)-(31,31) should be 0 or 1. */
    rlk_wg_ca_fill(&g_ca, 5, 5, &g_tilemap);

    /* Top-left area untouched. */
    assert(rlk_tilemap_get(&g_tilemap, 0, 0)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 4, 4)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 0, 31)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 31, 0)->type == 99);

    /* Inside the offset area: filled with wall(1) or floor(0). */
    int has_wall = 0, has_floor = 0;
    for (int y = 5; y < g_tilemap.height; y++) {
        for (int x = 5; x < g_tilemap.width; x++) {
            uint8_t t = rlk_tilemap_get(&g_tilemap, x, y)->type;
            assert(t == 0 || t == 1);
            if (t == 1) has_wall = 1;
            if (t == 0) has_floor = 1;
        }
    }
    assert(has_wall == 1);
    assert(has_floor == 1);
}

static void
test_fill_idempotent(void) {
    fixture_setup();

    /* fill re-generates deterministically: same result on second call. */
    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);
    uint8_t first[FIXTURE_MAP_W * FIXTURE_MAP_H];
    for (int y = 0; y < g_tilemap.height; y++)
        for (int x = 0; x < g_tilemap.width; x++)
            first[(size_t)y * (size_t)g_tilemap.width + (size_t)x] =
                rlk_tilemap_get(&g_tilemap, x, y)->type;

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);
    for (int y = 0; y < g_tilemap.height; y++)
        for (int x = 0; x < g_tilemap.width; x++)
            assert(rlk_tilemap_get(&g_tilemap, x, y)->type ==
                   first[(size_t)y * (size_t)g_tilemap.width + (size_t)x]);
}

static void
test_fill_one_iteration(void) {
    fixture_setup();
    g_ca.iterations = 1;

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    assert(g_ca.cur_iter == 1);
}

static void
test_fill_three_iterations(void) {
    fixture_setup();
    g_ca.iterations = 3;

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    assert(g_ca.cur_iter == 3);
}

static void
test_fill_small_map(void) {
    uint8_t grid[8 * 8];
    int region_ids[8 * 8];
    uint8_t tile_buf[8 * 8 * sizeof(rlk_tile_t)];
    rlk_tilemap_t tm;
    rlk_wg_ca_t ca = {
        .width = 8,
        .height = 8,
        .seed = 123,
        .fill_prob = 0.5f,
        .birth_low = 3,
        .birth_high = 5,
        .survive_low = 3,
        .survive_high = 5,
        .iterations = 3,
        .write = rlk_wg_ca_write_wallfloor,
        .write_data = &g_wf,
        .grid = grid,
        .region_ids = region_ids,
    };
    rlk_wg_ca_init(&ca);
    rlk_tilemap_init(&tm, 8, 8, tile_buf, sizeof(tile_buf));
    rlk_wg_ca_fill(&ca, 0, 0, &tm);

    assert(ca.cur_iter == 3);
    assert(ca.region_count <= 1);
}

static void
test_fill_different_rules(void) {
    fixture_setup();
    g_ca.birth_low = 5;
    g_ca.birth_high = 6;
    g_ca.survive_low = 5;
    g_ca.survive_high = 8;

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    assert(g_ca.cur_iter == FIXTURE_ITERATIONS);
    assert(g_ca.region_count <= 1);

    int floor_count = 0;
    int wall_count = 0;
    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        if (g_ca.grid[i] == 1) floor_count++;
        else if (g_ca.grid[i] == 0) wall_count++;
    }
    assert(floor_count > 0);
    assert(wall_count > 0);
}

static void
test_fill_high_fill_prob(void) {
    fixture_setup();
    g_ca.fill_prob = 0.8f;

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    int floor_count = 0;
    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        if (g_ca.grid[i] == 1) floor_count++;
    }
    assert(floor_count > 0);
}

static void
test_fill_low_fill_prob(void) {
    fixture_setup();
    g_ca.fill_prob = 0.1f;

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    assert(g_ca.region_count <= 1);
}

/* --- RAND ----------------------------------------------------------------- */

static void
test_rand_in_range(void) {
    fixture_setup();
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            float r = rlk_wg_ca_rand(&g_ca, x, y, 0);
            assert(r >= 0.0f && r < 1.0f);
        }
    }
}

static void
test_rand_deterministic(void) {
    fixture_setup();
    float r1 = rlk_wg_ca_rand(&g_ca, 5, 7, 42);
    float r2 = rlk_wg_ca_rand(&g_ca, 5, 7, 42);
    assert(r1 == r2);
}

static void
test_rand_salt_varies(void) {
    fixture_setup();
    float r1 = rlk_wg_ca_rand(&g_ca, 5, 7, 0);
    float r2 = rlk_wg_ca_rand(&g_ca, 5, 7, 1);
    assert(r1 != r2);
}

/* --- WRITER CALLBACK ------------------------------------------------------ */

static int g_writer_calls;
static int g_writer_has_wall;
static int g_writer_has_floor;

static void
test_writer_cb(const rlk_wg_ca_t *ca, const void *data, int x, int y,
               rlk_tile_t *out) {
    (void)ca; (void)data; (void)x; (void)y;
    g_writer_calls++;
    if (out->biome == 0) {
        g_writer_has_wall = 1;
        out->type = 10;
    } else {
        g_writer_has_floor = 1;
        out->type = 20;
    }
}

static void
test_fill_writer_callback(void) {
    fixture_setup();
    g_ca.write = test_writer_cb;
    g_writer_calls = 0;
    g_writer_has_wall = 0;
    g_writer_has_floor = 0;

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    assert(g_writer_calls == FIXTURE_MAP_W * FIXTURE_MAP_H);
    assert(g_writer_has_wall == 1);
    assert(g_writer_has_floor == 1);

    /* Tiles should have the writer's types, not the wallfloor defaults. */
    int has_10 = 0, has_20 = 0;
    for (int y = 0; y < g_tilemap.height; y++) {
        for (int x = 0; x < g_tilemap.width; x++) {
            uint8_t t = rlk_tilemap_get(&g_tilemap, x, y)->type;
            if (t == 10) has_10 = 1;
            if (t == 20) has_20 = 1;
        }
    }
    assert(has_10 == 1);
    assert(has_20 == 1);
}

static void
test_writer_cb_data(const rlk_wg_ca_t *ca, const void *data, int x, int y,
                    rlk_tile_t *out) {
    (void)ca; (void)x; (void)y;
    const rlk_wg_ca_wallfloor_t *d = (const rlk_wg_ca_wallfloor_t *)data;
    out->type = (out->biome == 0) ? d->wall_type : d->floor_type;
}

static void
test_fill_writer_with_data(void) {
    fixture_setup();
    static const rlk_wg_ca_wallfloor_t wd = { .wall_type = 7, .floor_type = 3 };
    g_ca.write = test_writer_cb_data;
    g_ca.write_data = &wd;

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    int has_7 = 0, has_3 = 0;
    for (int y = 0; y < g_tilemap.height; y++) {
        for (int x = 0; x < g_tilemap.width; x++) {
            uint8_t t = rlk_tilemap_get(&g_tilemap, x, y)->type;
            if (t == 7) has_7 = 1;
            if (t == 3) has_3 = 1;
        }
    }
    assert(has_7 == 1);
    assert(has_3 == 1);
}

/* --- BIOME / VARIANT (default fill) -------------------------------------- */

static void
test_fill_default_biome_variant(void) {
    fixture_setup();

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    int has_biome_zero = 0, has_biome_nonzero = 0;
    for (int y = 0; y < g_tilemap.height; y++) {
        for (int x = 0; x < g_tilemap.width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            /* Walls have biome 0, floors have biome >= 1 */
            if (tile->biome == 0) has_biome_zero = 1;
            else has_biome_nonzero = 1;
            /* Variant is filled (may be 0 by chance but unlikely for all) */
        }
    }
    assert(has_biome_zero == 1);
    assert(has_biome_nonzero == 1);

    /* At least some tiles should have non-zero variant */
    int has_variant = 0;
    for (int y = 0; y < g_tilemap.height; y++) {
        for (int x = 0; x < g_tilemap.width; x++) {
            if (rlk_tilemap_get(&g_tilemap, x, y)->variant != 0) {
                has_variant = 1;
                break;
            }
        }
        if (has_variant) break;
    }
    assert(has_variant == 1);
}

static void
test_fill_biome_matches_region(void) {
    fixture_setup();

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    for (int y = 0; y < g_ca.height; y++) {
        for (int x = 0; x < g_ca.width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            uint8_t cell = g_ca.grid[(size_t)y * (size_t)g_ca.width + (size_t)x];
            if (cell == 1) {
                assert(tile->biome > 0);
                assert(tile->biome == (uint8_t)g_ca.region_ids[(size_t)y * (size_t)g_ca.width + (size_t)x]);
            } else {
                assert(tile->biome == 0);
            }
        }
    }
}

/* --- CONNECT OPTION ------------------------------------------------------ */

static void
test_fill_connect_skip(void) {
    fixture_setup();
    g_ca.connect = 0;  /* skip connectivity */

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    /* Without connect, region_count may be > 1 */
    /* We can't assert region_count > 1 (could be 1 by chance),
       but biome should reflect separate regions */
    int distinct_biomes = 0;
    int seen[256] = {0};
    for (int y = 0; y < g_tilemap.height; y++) {
        for (int x = 0; x < g_tilemap.width; x++) {
            uint8_t b = rlk_tilemap_get(&g_tilemap, x, y)->biome;
            if (b > 0 && !seen[b]) {
                seen[b] = 1;
                distinct_biomes++;
            }
        }
    }
    /* At least 1 floor biome exists */
    assert(distinct_biomes >= 1);
}

static void
test_fill_connect_enabled(void) {
    fixture_setup();
    /* connect == 1: connectivity is enabled */
    assert(g_ca.connect == 1);

    rlk_wg_ca_fill(&g_ca, 0, 0, &g_tilemap);

    /* After connect, all floor is one region */
    assert(g_ca.region_count <= 1);

    /* All floor tiles have the same biome */
    int first_biome = -1;
    for (int y = 0; y < g_tilemap.height; y++) {
        for (int x = 0; x < g_tilemap.width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            if (tile->biome > 0) {
                if (first_biome == -1) first_biome = tile->biome;
                else assert(tile->biome == first_biome);
            }
        }
    }
}

int
main(void) {
    test_init_valid();
    test_init_default();
    test_fill_basic();
    test_fill_grid();
    test_fill_connected();
    test_fill_deterministic();
    test_fill_consistency();
    test_fill_offset();
    test_fill_idempotent();
    test_fill_one_iteration();
    test_fill_three_iterations();
    test_fill_small_map();
    test_fill_different_rules();
    test_fill_high_fill_prob();
    test_fill_low_fill_prob();
    test_rand_in_range();
    test_rand_deterministic();
    test_rand_salt_varies();
    test_fill_writer_callback();
    test_fill_writer_with_data();
    test_fill_default_biome_variant();
    test_fill_biome_matches_region();
    test_fill_connect_skip();
    test_fill_connect_enabled();

    return 0;
}
