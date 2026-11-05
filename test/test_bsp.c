#include <assert.h>
#include <limits.h>
#include <string.h>
#include "bsp.h"
#include "hash.h"

#define FIXTURE_MAP_W 64
#define FIXTURE_MAP_H 64
#define FIXTURE_GRID_SIZE (FIXTURE_MAP_W * FIXTURE_MAP_H)

/* Local writer (the lib provides no ready-made writer): wall/floor by biome. */
typedef struct { uint8_t wall_type, floor_type; } wf_t;

static void
write_wallfloor(uint64_t seed, const void *data, int x, int y,
                     rlk_tile_t *out) {
    const wf_t *wf = (const wf_t *)data;
    out->type = (out->biome == 0) ? wf->wall_type : wf->floor_type;
    out->variant = (uint8_t)(rlk_hash_normf(rlk_hash_at(seed, x, y, 0xCAFEULL)) * 255.0f);
}

static uint8_t g_grid[FIXTURE_GRID_SIZE];
static rlk_bsp_node_t g_nodes[RLK_BSP_MAX_NODES];
static rlk_bsp_t g_bsp;
static const wf_t g_wf = { .wall_type = 1, .floor_type = 0 };

static rlk_lattice_t g_win;
static uint8_t g_tile_buf[FIXTURE_MAP_W * FIXTURE_MAP_H * sizeof(rlk_tile_t)];
static rlk_tilemap_t g_tilemap;

static void
fixture_setup(void) {
    g_bsp = (rlk_bsp_t){
        .seed = 42,
        .write = write_wallfloor,
        .write_data = &g_wf,
        .grid = g_grid,
        .nodes = g_nodes,
    };
    rlk_bsp_init(&g_bsp);

    g_win = (rlk_lattice_t){ .w = FIXTURE_MAP_W, .h = FIXTURE_MAP_H,
                             .cellsize = 1, .origin_x = 0, .origin_y = 0 };
    rlk_tilemap_init(&g_tilemap, FIXTURE_MAP_W, FIXTURE_MAP_H,
                     g_tile_buf, sizeof(g_tile_buf));
}

static void
do_fill(void) {
    rlk_bsp_fill(&g_bsp, &g_win, &g_tilemap, NULL, NULL, NULL);
}

/* --- INIT (config only) -------------------------------------------------- */

static void
test_init_valid(void) {
    fixture_setup();

    assert(g_bsp.grid == g_grid);
    assert(g_bsp.nodes == g_nodes);
    assert(g_bsp.seed == 42);
    assert(g_bsp.write == write_wallfloor);
    assert(g_bsp.write_data == &g_wf);
    /* Defaults resolved from zero. */
    assert(g_bsp.min_room_size == 5);
    assert(g_bsp.max_depth == 7);
    assert(g_bsp.split_bias == 0.5f);
    assert(g_bsp.room_margin == 1);
}

static void
test_init_default(void) {
    rlk_bsp_t bsp;
    uint8_t grid[16 * 16];
    rlk_bsp_node_t nodes[RLK_BSP_MAX_NODES];

    static const wf_t wf = { .wall_type = 1, .floor_type = 0 };
    bsp = (rlk_bsp_t){
        .seed = 123,
        .write = write_wallfloor,
        .write_data = &wf,
        .grid = grid,
        .nodes = nodes,
    };
    rlk_bsp_init(&bsp);

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

    do_fill();

    int has_wall = 0, has_floor = 0;
    for (int y = 0; y < g_tilemap.h; y++) {
        for (int x = 0; x < g_tilemap.w; x++) {
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

    do_fill();

    int floor_count = 0;
    int wall_count = 0;
    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        if (g_grid[i] == 1) floor_count++;
        else if (g_grid[i] == 0) wall_count++;
    }
    assert(floor_count > 0);
    assert(wall_count > 0);
    assert(g_bsp.win_w == FIXTURE_MAP_W);
    assert(g_bsp.win_h == FIXTURE_MAP_H);
}

static void
test_fill_connected(void) {
    fixture_setup();

    do_fill();

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
    fixture_setup();

    uint8_t grid1[FIXTURE_GRID_SIZE];
    rlk_bsp_node_t nodes1[RLK_BSP_MAX_NODES];
    rlk_bsp_t bsp1 = {
        .seed = 42,
        .write = write_wallfloor,
        .write_data = &g_wf,
        .grid = grid1,
        .nodes = nodes1,
    };
    rlk_bsp_init(&bsp1);
    rlk_bsp_fill(&bsp1, &g_win, &g_tilemap, NULL, NULL, NULL);

    uint8_t grid2[FIXTURE_GRID_SIZE];
    rlk_bsp_node_t nodes2[RLK_BSP_MAX_NODES];
    rlk_bsp_t bsp2 = {
        .seed = 42,
        .write = write_wallfloor,
        .write_data = &g_wf,
        .grid = grid2,
        .nodes = nodes2,
    };
    rlk_bsp_init(&bsp2);
    rlk_bsp_fill(&bsp2, &g_win, &g_tilemap, NULL, NULL, NULL);

    for (size_t i = 0; i < FIXTURE_GRID_SIZE; i++) {
        assert(grid1[i] == grid2[i]);
    }
    assert(bsp1.room_count == bsp2.room_count);
    assert(bsp1.node_count == bsp2.node_count);
}

static void
test_fill_consistency(void) {
    fixture_setup();

    do_fill();

    for (int y = 0; y < g_bsp.win_h; y++) {
        for (int x = 0; x < g_bsp.win_w; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            uint8_t cell = g_bsp.grid[(size_t)y * (size_t)g_bsp.win_w + (size_t)x];
            uint8_t expected_type = (cell == 1) ? 0 : 1;
            assert(tile->type == expected_type);
        }
    }
}

/* Window at a world origin: tiles outside the window are untouched. */

static void
test_fill_window_origin(void) {
    fixture_setup();

    /* 59x25 window at world (5,5) over a 64x64 tilemap. */
    static uint8_t grid[59 * 25];
    static rlk_bsp_node_t nodes[RLK_BSP_MAX_NODES];
    rlk_lattice_t win = { .w = 59, .h = 25, .cellsize = 1,
                          .origin_x = 5, .origin_y = 5 };
    rlk_bsp_t bsp = {
        .seed = 42,
        .write = write_wallfloor,
        .write_data = &g_wf,
        .grid = grid,
        .nodes = nodes,
    };
    rlk_bsp_init(&bsp);

    /* Sentinel: mark all tiles as type 99. */
    for (int y = 0; y < g_tilemap.h; y++)
        for (int x = 0; x < g_tilemap.w; x++)
            rlk_tilemap_get(&g_tilemap, x, y)->type = 99;

    rlk_bsp_fill(&bsp, &win, &g_tilemap, NULL, NULL, NULL);

    /* Outside the window: untouched. */
    assert(rlk_tilemap_get(&g_tilemap, 0, 0)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 4, 4)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 0, 63)->type == 99);
    assert(rlk_tilemap_get(&g_tilemap, 63, 0)->type == 99);

    /* Inside the window: filled with wall(1) or floor(0). */
    int has_wall = 0, has_floor = 0;
    for (int y = 5; y < 30; y++) {
        for (int x = 5; x < 64; x++) {
            uint8_t t = rlk_tilemap_get(&g_tilemap, x, y)->type;
            assert(t == 0 || t == 1);
            if (t == 1) has_wall = 1;
            if (t == 0) has_floor = 1;
        }
    }
    assert(has_wall == 1);
    assert(has_floor == 1);
}

/* Mask weight 0: cells are not written (existing content preserved). */

static void
test_fill_mask(void) {
    fixture_setup();
    static uint8_t m_buf[FIXTURE_GRID_SIZE];
    for (int j = 0; j < FIXTURE_MAP_H; j++)
        for (int i = 0; i < FIXTURE_MAP_W; i++)
            m_buf[(size_t)j * FIXTURE_MAP_W + (size_t)i] = (i < 32) ? 0 : 255;
    rlk_mask_t mask = { .lattice = g_win, .weights = m_buf };

    /* Sentinel everywhere. */
    for (int y = 0; y < g_tilemap.h; y++)
        for (int x = 0; x < g_tilemap.w; x++)
            rlk_tilemap_get(&g_tilemap, x, y)->type = 99;

    rlk_bsp_fill(&g_bsp, &g_win, &g_tilemap, &mask, NULL, NULL);

    for (int y = 0; y < g_tilemap.h; y++) {
        for (int x = 0; x < g_tilemap.w; x++) {
            uint8_t t = rlk_tilemap_get(&g_tilemap, x, y)->type;
            if (x < 32)
                assert(t == 99);      /* masked out: untouched */
            else
                assert(t == 0 || t == 1);  /* written */
        }
    }
}

/* Reserved POI bbox: tiles inside are not written. */

static void
test_fill_poi_reserved(void) {
    fixture_setup();
    rlk_poi_t pbuf[1];
    rlk_poi_list_t pois = { pbuf, 1, 1 };
    pbuf[0] = (rlk_poi_t){ .bbox = { .x = 16, .y = 16, .w = 16, .h = 16 } };

    /* Sentinel everywhere. */
    for (int y = 0; y < g_tilemap.h; y++)
        for (int x = 0; x < g_tilemap.w; x++)
            rlk_tilemap_get(&g_tilemap, x, y)->type = 99;

    rlk_bsp_fill(&g_bsp, &g_win, &g_tilemap, NULL, &pois, NULL);

    assert(rlk_tilemap_get(&g_tilemap, 20, 20)->type == 99);  /* inside bbox */
    assert(rlk_tilemap_get(&g_tilemap, 31, 31)->type == 99);  /* inside bbox */
    for (int y = 0; y < g_tilemap.h; y++) {
        for (int x = 0; x < g_tilemap.w; x++) {
            uint8_t t = rlk_tilemap_get(&g_tilemap, x, y)->type;
            if (!rlk_rect_contains(&pbuf[0].bbox, x, y))
                assert(t == 0 || t == 1);  /* written */
        }
    }
}

/* spawn: entrance POI on the largest room. */

static void
test_fill_spawn(void) {
    fixture_setup();
    rlk_poi_t spawn = { .type = 7 };
    rlk_bsp_fill(&g_bsp, &g_win, &g_tilemap, NULL, NULL, &spawn);
    assert(spawn.type == 7);  /* caller's type preserved */
    assert(spawn.bbox.w >= g_bsp.min_room_size);
    assert(spawn.bbox.h >= g_bsp.min_room_size);
    assert(rlk_rect_contains(&spawn.bbox, spawn.x, spawn.y));
    /* The anchor is on a floor tile of the room. */
    assert(rlk_tilemap_get(&g_tilemap, spawn.x, spawn.y)->type == 0);

    /* Deterministic: same spawn on a second fill. */
    rlk_poi_t spawn2 = { .type = 7 };
    rlk_bsp_fill(&g_bsp, &g_win, &g_tilemap, NULL, NULL, &spawn2);
    assert(spawn2.x == spawn.x && spawn2.y == spawn.y);
    assert(spawn2.bbox.x == spawn.bbox.x && spawn2.bbox.y == spawn.bbox.y);
    assert(spawn2.bbox.w == spawn.bbox.w && spawn2.bbox.h == spawn.bbox.h);
    assert(spawn2.seed == spawn.seed);
}

/* cellsize 2: each cell is stamped as a uniform 2x2 tile block. */

static void
test_fill_cellsize(void) {
    rlk_lattice_t win = { .w = 32, .h = 32, .cellsize = 2,
                          .origin_x = 0, .origin_y = 0 };
    rlk_poi_t spawn = { .type = 3 };
    rlk_bsp_fill(&g_bsp, &win, &g_tilemap, NULL, NULL, &spawn);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t cell = g_bsp.grid[(size_t)y * 32 + (size_t)x];
            uint8_t expect = (cell == 1) ? 0 : 1;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++)
                    assert(rlk_tilemap_get(&g_tilemap, 2 * x + dx, 2 * y + dy)->type
                           == expect);
        }
    }
    /* The spawn bbox is a whole number of cells. */
    assert(spawn.bbox.w % 2 == 0 && spawn.bbox.h % 2 == 0);
}

static void
test_fill_idempotent(void) {
    fixture_setup();

    do_fill();
    uint8_t first[FIXTURE_MAP_W * FIXTURE_MAP_H];
    for (int y = 0; y < g_tilemap.h; y++)
        for (int x = 0; x < g_tilemap.w; x++)
            first[(size_t)y * (size_t)g_tilemap.w + (size_t)x] =
                rlk_tilemap_get(&g_tilemap, x, y)->type;

    do_fill();
    for (int y = 0; y < g_tilemap.h; y++)
        for (int x = 0; x < g_tilemap.w; x++)
            assert(rlk_tilemap_get(&g_tilemap, x, y)->type ==
                   first[(size_t)y * (size_t)g_tilemap.w + (size_t)x]);
}

static void
test_fill_small_map(void) {
    uint8_t grid[16 * 16];
    rlk_bsp_node_t nodes[RLK_BSP_MAX_NODES];
    uint8_t tile_buf[16 * 16 * sizeof(rlk_tile_t)];
    rlk_tilemap_t tm;
    rlk_lattice_t win = { .w = 16, .h = 16, .cellsize = 1,
                          .origin_x = 0, .origin_y = 0 };
    rlk_bsp_t bsp = {
        .seed = 123,
        .min_room_size = 3,
        .max_depth = 3,
        .write = write_wallfloor,
        .write_data = &g_wf,
        .grid = grid,
        .nodes = nodes,
    };
    rlk_bsp_init(&bsp);
    rlk_tilemap_init(&tm, 16, 16, tile_buf, sizeof(tile_buf));
    rlk_bsp_fill(&bsp, &win, &tm, NULL, NULL, NULL);

    assert(bsp.room_count >= 1);
    assert(bsp.node_count >= 1);
}

static void
test_fill_large_rooms(void) {
    fixture_setup();
    g_bsp.min_room_size = 10;
    g_bsp.max_depth = 3;

    do_fill();

    assert(g_bsp.room_count >= 1);

    /* All rooms should respect min_room_size. */
    for (int i = 0; i < g_bsp.node_count; i++) {
        rlk_bsp_node_t *n = &g_bsp.nodes[i];
        if (n->split == RLK_BSP_LEAF) {
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

    do_fill();

    assert(g_bsp.room_count > 1);
    assert(g_bsp.node_count <= RLK_BSP_MAX_NODES);
}

static void
test_fill_split_bias_h(void) {
    fixture_setup();
    g_bsp.split_bias = 1.0f;  /* always split along Y */
    g_bsp.min_room_size = 3;
    g_bsp.max_depth = 4;

    do_fill();

    /* Horizontal splits dominate: a vertical split is only allowed on
       nodes too short to split horizontally. */
    int n_h = 0, n_v = 0;
    for (int i = 0; i < g_bsp.node_count; i++) {
        const rlk_bsp_node_t *n = &g_bsp.nodes[i];
        if (n->split == RLK_BSP_SPLIT_H) n_h++;
        else if (n->split == RLK_BSP_SPLIT_V) {
            n_v++;
            assert(n->h < 2 * g_bsp.min_room_size);
        }
    }
    assert(n_h > 0);
    assert(n_v < n_h);
}

static void
test_fill_split_bias_v(void) {
    fixture_setup();
    g_bsp.split_bias = 0.0f;  /* always split along X */
    g_bsp.min_room_size = 3;
    g_bsp.max_depth = 4;

    do_fill();

    /* Vertical splits dominate: a horizontal split is only allowed on
       nodes too narrow to split vertically. */
    int n_h = 0, n_v = 0;
    for (int i = 0; i < g_bsp.node_count; i++) {
        const rlk_bsp_node_t *n = &g_bsp.nodes[i];
        if (n->split == RLK_BSP_SPLIT_H) {
            n_h++;
            assert(n->w < 2 * g_bsp.min_room_size);
        } else if (n->split == RLK_BSP_SPLIT_V) n_v++;
    }
    assert(n_v > 0);
    assert(n_h < n_v);
}

/* --- WRITER CALLBACK ------------------------------------------------------ */

static int g_writer_calls;
static int g_writer_has_wall;
static int g_writer_has_floor;

static void
writer_cb(uint64_t seed, const void *data, int x, int y,
               rlk_tile_t *out) {
    (void)seed; (void)data; (void)x; (void)y;
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
    g_bsp.write = writer_cb;
    g_writer_calls = 0;
    g_writer_has_wall = 0;
    g_writer_has_floor = 0;

    do_fill();

    assert(g_writer_calls == FIXTURE_MAP_W * FIXTURE_MAP_H);
    assert(g_writer_has_wall == 1);
    assert(g_writer_has_floor == 1);

    int has_10 = 0, has_20 = 0;
    for (int y = 0; y < g_tilemap.h; y++) {
        for (int x = 0; x < g_tilemap.w; x++) {
            uint8_t t = rlk_tilemap_get(&g_tilemap, x, y)->type;
            if (t == 10) has_10 = 1;
            if (t == 20) has_20 = 1;
        }
    }
    assert(has_10 == 1);
    assert(has_20 == 1);
}

static void
writer_cb_data(uint64_t seed, const void *data, int x, int y,
                    rlk_tile_t *out) {
    (void)seed; (void)x; (void)y;
    const wf_t *d = (const wf_t *)data;
    out->type = (out->biome == 0) ? d->wall_type : d->floor_type;
}

static void
test_fill_writer_with_data(void) {
    fixture_setup();
    static const wf_t wd = { .wall_type = 7, .floor_type = 3 };
    g_bsp.write = writer_cb_data;
    g_bsp.write_data = &wd;

    do_fill();

    int has_7 = 0, has_3 = 0;
    for (int y = 0; y < g_tilemap.h; y++) {
        for (int x = 0; x < g_tilemap.w; x++) {
            uint8_t t = rlk_tilemap_get(&g_tilemap, x, y)->type;
            if (t == 7) has_7 = 1;
            if (t == 3) has_3 = 1;
        }
    }
    assert(has_7 == 1);
    assert(has_3 == 1);
}

/* The writer receives the world position of each cell's top-left tile. */
static int g_wx_min, g_wx_max, g_wy_min, g_wy_max;

static void
writer_coords(uint64_t seed, const void *data, int x, int y,
              rlk_tile_t *out) {
    (void)seed; (void)data;
    if (x < g_wx_min) g_wx_min = x;
    if (x > g_wx_max) g_wx_max = x;
    if (y < g_wy_min) g_wy_min = y;
    if (y > g_wy_max) g_wy_max = y;
    out->type = 1;
}

static void
test_fill_writer_coords(void) {
    fixture_setup();

    /* 27x27 window at world origin (5,5) over the 64x64 tilemap. */
    static uint8_t grid[27 * 27];
    static rlk_bsp_node_t nodes[RLK_BSP_MAX_NODES];
    rlk_lattice_t win = { .w = 27, .h = 27, .cellsize = 1,
                          .origin_x = 5, .origin_y = 5 };
    rlk_bsp_t bsp = {
        .seed = 42,
        .write = writer_coords,
        .write_data = NULL,
        .grid = grid,
        .nodes = nodes,
    };
    rlk_bsp_init(&bsp);
    g_wx_min = INT_MAX; g_wx_max = -1;
    g_wy_min = INT_MAX; g_wy_max = -1;

    rlk_bsp_fill(&bsp, &win, &g_tilemap, NULL, NULL, NULL);

    assert(g_wx_min == 5 && g_wx_max == 5 + 27 - 1);
    assert(g_wy_min == 5 && g_wy_max == 5 + 27 - 1);
}

/* --- BIOME ---------------------------------------------------------------- */

static void
test_fill_biome_matches_grid(void) {
    fixture_setup();

    do_fill();

    for (int y = 0; y < g_bsp.win_h; y++) {
        for (int x = 0; x < g_bsp.win_w; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(&g_tilemap, x, y);
            uint8_t cell = g_bsp.grid[(size_t)y * (size_t)g_bsp.win_w + (size_t)x];
            assert(tile->biome == cell);
        }
    }
}

/* --- DIFFERENT SEEDS ----------------------------------------------------- */

static void
test_fill_different_seeds(void) {
    fixture_setup();

    uint8_t grid1[FIXTURE_GRID_SIZE];
    rlk_bsp_node_t nodes1[RLK_BSP_MAX_NODES];
    rlk_bsp_t bsp1 = {
        .seed = 1,
        .write = write_wallfloor, .write_data = &g_wf,
        .grid = grid1, .nodes = nodes1,
    };
    rlk_bsp_init(&bsp1);
    rlk_bsp_fill(&bsp1, &g_win, &g_tilemap, NULL, NULL, NULL);

    uint8_t grid2[FIXTURE_GRID_SIZE];
    rlk_bsp_node_t nodes2[RLK_BSP_MAX_NODES];
    rlk_bsp_t bsp2 = {
        .seed = 2,
        .write = write_wallfloor, .write_data = &g_wf,
        .grid = grid2, .nodes = nodes2,
    };
    rlk_bsp_init(&bsp2);
    rlk_bsp_fill(&bsp2, &g_win, &g_tilemap, NULL, NULL, NULL);

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
    test_fill_connected();
    test_fill_deterministic();
    test_fill_consistency();
    test_fill_window_origin();
    test_fill_mask();
    test_fill_poi_reserved();
    test_fill_spawn();
    test_fill_cellsize();
    test_fill_idempotent();
    test_fill_small_map();
    test_fill_large_rooms();
    test_fill_deep_tree();
    test_fill_split_bias_h();
    test_fill_split_bias_v();
    test_fill_writer_callback();
    test_fill_writer_with_data();
    test_fill_writer_coords();
    test_fill_biome_matches_grid();
    test_fill_different_seeds();

    return 0;
}
