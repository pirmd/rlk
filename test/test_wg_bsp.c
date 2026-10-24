#include <assert.h>
#include <string.h>
#include "wg_bsp.h"

#define FIXTURE_MAP_W 64
#define FIXTURE_MAP_H 64
#define FIXTURE_GRID_SIZE (FIXTURE_MAP_W * FIXTURE_MAP_H)

static uint8_t g_grid[FIXTURE_GRID_SIZE];
static rlk_wg_bsp_node_t g_nodes[RLK_WG_BSP_MAX_NODES];
static rlk_wg_bsp_t g_bsp;
static const rlk_wg_bsp_wallfloor_t g_wf = { .wall_type = 1, .floor_type = 0 };

static uint8_t g_tile_buf[FIXTURE_MAP_W * FIXTURE_MAP_H * sizeof(rlk_tile_t)];
static rlk_tilemap_t g_tilemap;

static void
fixture_setup(void) {
    g_bsp = (rlk_wg_bsp_t){
        .width = FIXTURE_MAP_W,
        .height = FIXTURE_MAP_H,
        .seed = 42,
        .write = rlk_wg_bsp_write_wallfloor,
        .write_data = &g_wf,
        .grid = g_grid,
        .nodes = g_nodes,
    };
    rlk_wg_bsp_init(&g_bsp);

    rlk_tilemap_init(&g_tilemap, FIXTURE_MAP_W, FIXTURE_MAP_H,
                     g_tile_buf, sizeof(g_tile_buf));
}

/* --- INIT (config only) -------------------------------------------------- */

static void
test_init_valid(void) {
    fixture_setup();

    assert(g_bsp.grid == g_grid);
    assert(g_bsp.nodes == g_nodes);
    assert(g_bsp.width == FIXTURE_MAP_W);
    assert(g_bsp.height == FIXTURE_MAP_H);
    assert(g_bsp.seed == 42);
    assert(g_bsp.write == rlk_wg_bsp_write_wallfloor);
    assert(g_bsp.write_data == &g_wf);
    /* Defaults resolved from zero. */
    assert(g_bsp.min_room_size == 5);
    assert(g_bsp.max_depth == 7);
    assert(g_bsp.split_bias == 0.5f);
    assert(g_bsp.room_margin == 1);
}

static void
test_init_default(void) {
    rlk_wg_bsp_t bsp;
    uint8_t grid[16 * 16];
    rlk_wg_bsp_node_t nodes[RLK_WG_BSP_MAX_NODES];

    static const rlk_wg_bsp_wallfloor_t wf = { .wall_type = 1, .floor_type = 0 };
    bsp = (rlk_wg_bsp_t){
        .width = 16,
        .height = 16,
        .seed = 123,
        .write = rlk_wg_bsp_write_wallfloor,
        .write_data = &wf,
        .grid = grid,
        .nodes = nodes,
    };
    rlk_wg_bsp_init(&bsp);

    assert(bsp.min_room_size == 5);
    assert(bsp.max_depth == 7);
    assert(bsp.split_bias == 0.5f);
    assert(bsp.room_margin == 1);
    assert(bsp.seed == 123);
}

/* --- FILL (generate + render) -------------------------------------------- */

static void
test_fill_basic(void) {
    fixture_setup();

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

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

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    int floor_count = 0;
    int wall_count = 0;
    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        if (g_grid[i] == 1) floor_count++;
        else if (g_grid[i] == 0) wall_count++;
    }
    assert(floor_count > 0);
    assert(wall_count > 0);
}

static void
test_fill_rooms_created(void) {
    fixture_setup();

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    assert(g_bsp.room_count > 0);
    assert(g_bsp.node_count > 0);
    assert(g_bsp.node_count <= RLK_WG_BSP_MAX_NODES);
    /* Each leaf is a room; internal nodes = leaves - 1 (binary tree). */
    assert(g_bsp.room_count >= 1);
}

static void
test_fill_connected(void) {
    fixture_setup();

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    /* BSP guarantees connectivity: all floor cells should be reachable.
       We verify by flood-fill from the first floor cell. */
    int start_x = -1, start_y = -1;
    for (int y = 0; y < FIXTURE_MAP_H && start_x < 0; y++) {
        for (int x = 0; x < FIXTURE_MAP_W; x++) {
            if (g_grid[(size_t)y * FIXTURE_MAP_W + (size_t)x] == 1) {
                start_x = x;
                start_y = y;
                break;
            }
        }
    }
    assert(start_x >= 0);

    /* Simple flood fill (4-connected). */
    static uint8_t visited[FIXTURE_GRID_SIZE];
    memset(visited, 0, sizeof(visited));

    /* BFS queue. */
    static int qx[FIXTURE_GRID_SIZE], qy[FIXTURE_GRID_SIZE];
    int head = 0, tail = 0;
    qx[tail] = start_x; qy[tail] = start_y; tail++;
    visited[(size_t)start_y * FIXTURE_MAP_W + (size_t)start_x] = 1;

    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx < 0 || nx >= FIXTURE_MAP_W || ny < 0 || ny >= FIXTURE_MAP_H)
                continue;
            size_t nidx = (size_t)ny * FIXTURE_MAP_W + (size_t)nx;
            if (g_grid[nidx] == 1 && !visited[nidx]) {
                visited[nidx] = 1;
                qx[tail] = nx; qy[tail] = ny; tail++;
            }
        }
    }

    /* All floor cells must be visited. */
    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        if (g_grid[i] == 1) {
            assert(visited[i] == 1);
        }
    }
}

static void
test_fill_deterministic(void) {
    uint8_t grid1[FIXTURE_GRID_SIZE];
    rlk_wg_bsp_node_t nodes1[RLK_WG_BSP_MAX_NODES];
    rlk_wg_bsp_t bsp1 = {
        .width = FIXTURE_MAP_W,
        .height = FIXTURE_MAP_H,
        .seed = 42,
        .write = rlk_wg_bsp_write_wallfloor,
        .write_data = &g_wf,
        .grid = grid1,
        .nodes = nodes1,
    };
    rlk_wg_bsp_init(&bsp1);
    rlk_wg_bsp_fill(&bsp1, 0, 0, &g_tilemap);

    uint8_t grid2[FIXTURE_GRID_SIZE];
    rlk_wg_bsp_node_t nodes2[RLK_WG_BSP_MAX_NODES];
    rlk_wg_bsp_t bsp2 = {
        .width = FIXTURE_MAP_W,
        .height = FIXTURE_MAP_H,
        .seed = 42,
        .write = rlk_wg_bsp_write_wallfloor,
        .write_data = &g_wf,
        .grid = grid2,
        .nodes = nodes2,
    };
    rlk_wg_bsp_init(&bsp2);
    rlk_wg_bsp_fill(&bsp2, 0, 0, &g_tilemap);

    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        assert(grid1[i] == grid2[i]);
    }
    assert(bsp1.room_count == bsp2.room_count);
    assert(bsp1.node_count == bsp2.node_count);
}

static void
test_fill_consistency(void) {
    fixture_setup();

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    for (int y = 0; y < g_bsp.height; y++) {
        for (int x = 0; x < g_bsp.width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            uint8_t cell = g_bsp.grid[(size_t)y * (size_t)g_bsp.width + (size_t)x];
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

    /* Fill at offset (5, 5). */
    rlk_wg_bsp_fill(&g_bsp, 5, 5, &g_tilemap);

    /* Top-left area untouched. */
    assert(rlk_tilemap_get(&g_tilemap, 0, 0)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 4, 4)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 0, 63)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 63, 0)->type == 99);

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

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);
    uint8_t first[FIXTURE_MAP_W * FIXTURE_MAP_H];
    for (int y = 0; y < g_tilemap.height; y++)
        for (int x = 0; x < g_tilemap.width; x++)
            first[(size_t)y * (size_t)g_tilemap.width + (size_t)x] =
                rlk_tilemap_get(&g_tilemap, x, y)->type;

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);
    for (int y = 0; y < g_tilemap.height; y++)
        for (int x = 0; x < g_tilemap.width; x++)
            assert(rlk_tilemap_get(&g_tilemap, x, y)->type ==
                   first[(size_t)y * (size_t)g_tilemap.width + (size_t)x]);
}

static void
test_fill_small_map(void) {
    uint8_t grid[16 * 16];
    rlk_wg_bsp_node_t nodes[RLK_WG_BSP_MAX_NODES];
    uint8_t tile_buf[16 * 16 * sizeof(rlk_tile_t)];
    rlk_tilemap_t tm;
    rlk_wg_bsp_t bsp = {
        .width = 16,
        .height = 16,
        .seed = 123,
        .min_room_size = 3,
        .max_depth = 3,
        .write = rlk_wg_bsp_write_wallfloor,
        .write_data = &g_wf,
        .grid = grid,
        .nodes = nodes,
    };
    rlk_wg_bsp_init(&bsp);
    rlk_tilemap_init(&tm, 16, 16, tile_buf, sizeof(tile_buf));
    rlk_wg_bsp_fill(&bsp, 0, 0, &tm);

    assert(bsp.room_count >= 1);
    assert(bsp.node_count >= 1);
}

static void
test_fill_large_rooms(void) {
    fixture_setup();
    g_bsp.min_room_size = 10;
    g_bsp.max_depth = 3;

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    assert(g_bsp.room_count >= 1);

    /* All rooms should respect min_room_size. */
    for (int i = 0; i < g_bsp.node_count; i++) {
        rlk_wg_bsp_node_t *n = &g_bsp.nodes[i];
        if (n->split == 0) {  /* leaf */
            assert(n->rw >= g_bsp.min_room_size);
            assert(n->rh >= g_bsp.min_room_size);
        }
    }
}

static void
test_fill_deep_tree(void) {
    fixture_setup();
    g_bsp.min_room_size = 3;
    g_bsp.max_depth = 8;

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    assert(g_bsp.room_count > 1);
    assert(g_bsp.node_count <= RLK_WG_BSP_MAX_NODES);
}

static void
test_fill_split_bias_vertical(void) {
    fixture_setup();
    g_bsp.split_bias = 1.0f;  /* prefer horizontal split (along Y) */
    g_bsp.min_room_size = 3;
    g_bsp.max_depth = 4;

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    /* With bias 1.0 on a square map, horizontal splits should appear. */
    int n_h = 0;
    for (int i = 0; i < g_bsp.node_count; i++) {
        rlk_wg_bsp_node_t *n = &g_bsp.nodes[i];
        if (n->split == 1) n_h++;
    }
    assert(n_h > 0);
}

static void
test_fill_split_bias_horizontal(void) {
    fixture_setup();
    g_bsp.split_bias = 0.0f;  /* prefer vertical split (along X) */
    g_bsp.min_room_size = 3;
    g_bsp.max_depth = 4;

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    /* With bias 0.0 on a square map, vertical splits should appear. */
    int n_v = 0;
    for (int i = 0; i < g_bsp.node_count; i++) {
        rlk_wg_bsp_node_t *n = &g_bsp.nodes[i];
        if (n->split == 2) n_v++;
    }
    assert(n_v > 0);
}

/* --- RAND ----------------------------------------------------------------- */

static void
test_rand_in_range(void) {
    fixture_setup();
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            float r = rlk_wg_bsp_rand(&g_bsp, x, y, 0);
            assert(r >= 0.0f && r < 1.0f);
        }
    }
}

static void
test_rand_deterministic(void) {
    fixture_setup();
    float r1 = rlk_wg_bsp_rand(&g_bsp, 5, 7, 42);
    float r2 = rlk_wg_bsp_rand(&g_bsp, 5, 7, 42);
    assert(r1 == r2);
}

static void
test_rand_salt_varies(void) {
    fixture_setup();
    float r1 = rlk_wg_bsp_rand(&g_bsp, 5, 7, 0);
    float r2 = rlk_wg_bsp_rand(&g_bsp, 5, 7, 1);
    assert(r1 != r2);
}

/* --- WRITER CALLBACK ------------------------------------------------------ */

static int g_writer_calls;
static int g_writer_has_wall;
static int g_writer_has_floor;

static void
test_writer_cb(const rlk_wg_bsp_t *bsp, const void *data, int x, int y,
               rlk_tile_t *out) {
    (void)bsp; (void)data; (void)x; (void)y;
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
    g_bsp.write = test_writer_cb;
    g_writer_calls = 0;
    g_writer_has_wall = 0;
    g_writer_has_floor = 0;

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    assert(g_writer_calls == FIXTURE_MAP_W * FIXTURE_MAP_H);
    assert(g_writer_has_wall == 1);
    assert(g_writer_has_floor == 1);

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
test_writer_cb_data(const rlk_wg_bsp_t *bsp, const void *data, int x, int y,
                    rlk_tile_t *out) {
    (void)bsp; (void)x; (void)y;
    const rlk_wg_bsp_wallfloor_t *d = (const rlk_wg_bsp_wallfloor_t *)data;
    out->type = (out->biome == 0) ? d->wall_type : d->floor_type;
}

static void
test_fill_writer_with_data(void) {
    fixture_setup();
    static const rlk_wg_bsp_wallfloor_t wd = { .wall_type = 7, .floor_type = 3 };
    g_bsp.write = test_writer_cb_data;
    g_bsp.write_data = &wd;

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

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

/* --- BIOME / VARIANT ------------------------------------------------------ */

static void
test_fill_default_biome_variant(void) {
    fixture_setup();

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    int has_biome_zero = 0, has_biome_one = 0;
    for (int y = 0; y < g_tilemap.height; y++) {
        for (int x = 0; x < g_tilemap.width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            if (tile->biome == 0) has_biome_zero = 1;
            else if (tile->biome == 1) has_biome_one = 1;
        }
    }
    assert(has_biome_zero == 1);
    assert(has_biome_one == 1);

    /* At least some tiles should have non-zero variant. */
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
test_fill_biome_matches_grid(void) {
    fixture_setup();

    rlk_wg_bsp_fill(&g_bsp, 0, 0, &g_tilemap);

    for (int y = 0; y < g_bsp.height; y++) {
        for (int x = 0; x < g_bsp.width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            uint8_t cell = g_bsp.grid[(size_t)y * (size_t)g_bsp.width + (size_t)x];
            assert(tile->biome == cell);
        }
    }
}

/* --- DIFFERENT SEEDS ----------------------------------------------------- */

static void
test_fill_different_seeds(void) {
    uint8_t grid1[FIXTURE_GRID_SIZE];
    rlk_wg_bsp_node_t nodes1[RLK_WG_BSP_MAX_NODES];
    rlk_wg_bsp_t bsp1 = {
        .width = FIXTURE_MAP_W, .height = FIXTURE_MAP_H, .seed = 1,
        .write = rlk_wg_bsp_write_wallfloor, .write_data = &g_wf,
        .grid = grid1, .nodes = nodes1,
    };
    rlk_wg_bsp_init(&bsp1);
    rlk_wg_bsp_fill(&bsp1, 0, 0, &g_tilemap);

    uint8_t grid2[FIXTURE_GRID_SIZE];
    rlk_wg_bsp_node_t nodes2[RLK_WG_BSP_MAX_NODES];
    rlk_wg_bsp_t bsp2 = {
        .width = FIXTURE_MAP_W, .height = FIXTURE_MAP_H, .seed = 2,
        .write = rlk_wg_bsp_write_wallfloor, .write_data = &g_wf,
        .grid = grid2, .nodes = nodes2,
    };
    rlk_wg_bsp_init(&bsp2);
    rlk_wg_bsp_fill(&bsp2, 0, 0, &g_tilemap);

    /* Different seeds should produce different grids (very likely). */
    int diff = 0;
    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        if (grid1[i] != grid2[i]) diff = 1;
    }
    assert(diff == 1);
}

int
main(void) {
    test_init_valid();
    test_init_default();
    test_fill_basic();
    test_fill_grid();
    test_fill_rooms_created();
    test_fill_connected();
    test_fill_deterministic();
    test_fill_consistency();
    test_fill_offset();
    test_fill_idempotent();
    test_fill_small_map();
    test_fill_large_rooms();
    test_fill_deep_tree();
    test_fill_split_bias_vertical();
    test_fill_split_bias_horizontal();
    test_rand_in_range();
    test_rand_deterministic();
    test_rand_salt_varies();
    test_fill_writer_callback();
    test_fill_writer_with_data();
    test_fill_default_biome_variant();
    test_fill_biome_matches_grid();
    test_fill_different_seeds();

    return 0;
}
