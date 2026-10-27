/*
 * Fantasy world demo: POI-driven pipeline.
 *
 * Pipeline: elevation grid -> flow (rivers) -> wg_nf terrain
 *           -> POI detection (caves near rivers, dungeons on hills)
 *           -> local generation (wg_ca for caves, wg_bsp for dungeons)
 *
 * Terminal-only: no PXL dependency. Prints an ASCII map.
 */

#include <stdint.h>
#include <stdio.h>

#include "rlk.h"

/* --- MAP CONSTANTS ------------------------------------------------------- */
#define MAP_W   80
#define MAP_H   40
#define GRID_W  32
#define GRID_H  32
#define GRID_CELL 8
#define SEA_LEVEL 0.35f
#define MAX_POI_SZ 48

/* Tile types for display. */
enum {
    T_WATER = 0,
    T_PLAIN,
    T_FOREST,
    T_MOUNTAIN,
    T_CAVE_WALL,
    T_CAVE_FLOOR,
    T_DUNGEON_WALL,
    T_DUNGEON_FLOOR,
    T_COUNT
};

static const char TILE_CHARS[T_COUNT] = {
    '~', '.', 'T', '^', '#', ' ', '#', ',',
};

/* --- ELEVATION GRID FUNCTION --------------------------------------------- */

static float
elev_fn(float px, float py, const void *user) {
    const rlk_noise_t *n = (const rlk_noise_t *)user;
    return rlk_noise_fbm(n, px, py, 4, 0.03f, 0.5f);
}

/* --- DISPLAY ------------------------------------------------------------- */

static void
display(const rlk_tilemap_t *tm) {
    for (int y = 0; y < tm->h; y++) {
        for (int x = 0; x < tm->w; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(tm, x, y);
            int idx = (t->type < T_COUNT) ? t->type : 0;
            putchar(TILE_CHARS[idx]);
        }
        putchar('\n');
    }
}

/* --- CUSTOM WRITER: plain zone ------------------------------------------- */

static void
write_plain(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)ctx;
    (void)data;
    out->type = T_PLAIN;
}

/* --- MAIN ---------------------------------------------------------------- */

int
main(void) {
    /* Static buffers: no allocation. */
    static rlk_tile_t tile_buf[MAP_W * MAP_H];
    static float grid_buf[GRID_W * GRID_H];
    static uint16_t flow_fill[GRID_W * GRID_H];
    static uint8_t flow_dir[GRID_W * GRID_H];
    static uint32_t flow_acc[GRID_W * GRID_H];
    static int flow_order[GRID_W * GRID_H];
    static uint64_t flow_scratch[GRID_W * GRID_H];
    static uint8_t ca_grid[MAX_POI_SZ * MAX_POI_SZ];
    static int ca_regions[MAX_POI_SZ * MAX_POI_SZ];
    static uint8_t bsp_grid[MAX_POI_SZ * MAX_POI_SZ];
    static rlk_wg_bsp_node_t bsp_nodes[RLK_WG_BSP_MAX_NODES];

    /* Tilemap. */
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP_W, MAP_H, tile_buf, sizeof(tile_buf));

    /* Elevation grid: coarse noise field. */
    rlk_grid_t elev = { .lattice = { .w = GRID_W, .h = GRID_H, .cellsize = GRID_CELL,
                           .origin_x = 0, .origin_y = 0 }, .v = grid_buf };
    rlk_noise_t elev_noise;
    rlk_noise_seed(&elev_noise, 0xE1E7A7E0ULL);
    for (int j = 0; j < GRID_H; j++)
        for (int i = 0; i < GRID_W; i++) {
            float px, py;
            rlk_grid_center(&elev, i, j, &px, &py);
            grid_buf[(size_t)j * GRID_W + i] = elev_fn(px, py, &elev_noise);
        }

    /* Flow: depression filling + D8 drainage + accumulation. */
    rlk_wg_flow_t flow = { .w = GRID_W, .h = GRID_H,
                           .fill = flow_fill, .dir = flow_dir,
                           .acc = flow_acc, .order = flow_order };
    rlk_err_t err = rlk_wg_flow_build(&flow, &elev, SEA_LEVEL, flow_scratch);
    if (err != RLK_OK) {
        fprintf(stderr, "flow_build: %s\n", rlk_err_to_string(err));
        return 1;
    }

    /* POI list: populated by wg_nf_poi stages during fill. */
    rlk_wg_poi_list_t pois;
    rlk_wg_poi_list_init(&pois);

    /* wg_nf: 3 sampled fields + 3 derived fields. */
    rlk_wg_nf_t nf = {
        .seed = 0xFA17A5ULL,
        .nsampled = 3,
        .nderived = 3,
        .naxes = 2,
        .nzones = 4,
        .depth_scale = 0.15f,
        .transition_width = 0.08f,
    };

    /* Sampled fields: elevation, humidity, temperature. */
    nf.fields[0] = (rlk_wg_nf_field_t){
        .kind = RLK_WG_NF_FBM, .seed = 0xE1E7ULL, .freq = 0.030f,
    };
    nf.fields[1] = (rlk_wg_nf_field_t){
        .kind = RLK_WG_NF_FBM, .seed = 0x1111ULL, .freq = 0.025f,
    };
    nf.fields[2] = (rlk_wg_nf_field_t){
        .kind = RLK_WG_NF_FBM, .seed = 0x2222ULL, .freq = 0.010f,
    };

    /* Axes: elevation (field 0, jittered by humidity) + temperature (field 2). */
    nf.axes[0] = (rlk_wg_nf_axis_t){
        .field = 0, .jitter_field = 1, .jitter_amp = 0.03f,
    };
    nf.axes[1] = (rlk_wg_nf_axis_t){ .field = 2 };

    /* Zones: water, plain (warm), forest (cool), mountain. */
    static const rlk_tile_t water_tile    = { .type = T_WATER };
    static const rlk_tile_t forest_tile   = { .type = T_FOREST };
    static const rlk_tile_t mountain_tile = { .type = T_MOUNTAIN };

    nf.zones[0] = (rlk_wg_nf_zone_t){
        .lo = { -RLK_WG_NF_OPEN, -RLK_WG_NF_OPEN },
        .hi = { 0.30f, RLK_WG_NF_OPEN },
        .write = rlk_wg_nf_write_const, .data = &water_tile,
    };
    nf.zones[1] = (rlk_wg_nf_zone_t){  /* plain: warm lowland */
        .lo = { 0.25f, 0.45f },
        .hi = { 0.55f, RLK_WG_NF_OPEN },
        .write = write_plain, .data = NULL,
    };
    nf.zones[2] = (rlk_wg_nf_zone_t){  /* forest: cool lowland */
        .lo = { 0.25f, -RLK_WG_NF_OPEN },
        .hi = { 0.55f, 0.45f },
        .write = rlk_wg_nf_write_const, .data = &forest_tile,
    };
    nf.zones[3] = (rlk_wg_nf_zone_t){  /* mountain */
        .lo = { 0.50f, -RLK_WG_NF_OPEN },
        .hi = { RLK_WG_NF_OPEN, RLK_WG_NF_OPEN },
        .write = rlk_wg_nf_write_const, .data = &mountain_tile,
    };

    /* Stage 1: coarse elevation grid -> field[3]. */
    rlk_wg_nf_grid_t grid_stage = {
        .grid = &elev,
        .slot = 0,
        .scale = 1.0f,
        .offset = 0.0f,
    };
    rlk_wg_nf_grid_add(&nf, &grid_stage);

    /* Stage 2: flow accumulation -> field[4], lake depth -> field[5]. */
    rlk_wg_nf_flow_t flow_stage = {
        .flow = &flow,
        .elev = &elev,
        .sea = SEA_LEVEL,
        .slot_acc = 1,
        .slot_dir = RLK_WG_NF_FLOW_NONE,
        .slot_lake = 2,
        .acc_scale = 500.0f,
    };
    rlk_wg_nf_flow_add(&nf, &flow_stage);

    /* Stage 3: detect caves (high humidity = wet areas). */
    rlk_wg_nf_poi_t poi_cave = {
        .list = &pois,
        .kind = RLK_WG_POI_CAVE,
        .salt = 0xCAFEULL,
        .field = 1,            /* humidity */
        .threshold = 0.52f,
        .density = 0.015f,
        .size = 32,
    };
    rlk_wg_nf_poi_add(&nf, &poi_cave);

    /* Stage 4: detect dungeons (coarse elevation grid = hills). */
    rlk_wg_nf_poi_t poi_dungeon = {
        .list = &pois,
        .kind = RLK_WG_POI_DUNGEON,
        .salt = 0xD06EULL,
        .field = 3,            /* derived: elevation from coarse grid */
        .threshold = 0.55f,
        .density = 0.015f,
        .size = 40,
    };
    rlk_wg_nf_poi_add(&nf, &poi_dungeon);

    /* Init and fill: generates terrain AND populates POI list. */
    rlk_wg_nf_init(&nf);
    rlk_wg_nf_fill(&nf, 0, 0, &tm);

    /* Generate local content for each detected POI. */
    for (int i = 0; i < pois.count; i++) {
        const rlk_wg_poi_t *poi = &pois.items[i];
        int sz = (int)poi->size;
        int ox = poi->wx - sz / 2;
        int oy = poi->wy - sz / 2;

        if (poi->kind == RLK_WG_POI_CAVE) {
            static const rlk_wg_ca_wallfloor_t wf = {
                .wall_type = T_CAVE_WALL, .floor_type = T_CAVE_FLOOR,
            };
            rlk_wg_ca_t ca = {
                .width = sz, .height = sz,
                .seed = poi->seed,
                .fill_prob = 0.45f,
                .iterations = 5,
                .connect = 1,
                .write = rlk_wg_ca_write_wallfloor,
                .write_data = &wf,
                .grid = ca_grid,
                .region_ids = ca_regions,
            };
            rlk_wg_ca_init(&ca);
            rlk_wg_ca_fill(&ca, ox, oy, &tm);
        } else if (poi->kind == RLK_WG_POI_DUNGEON) {
            static const rlk_wg_bsp_wallfloor_t wf = {
                .wall_type = T_DUNGEON_WALL, .floor_type = T_DUNGEON_FLOOR,
            };
            rlk_wg_bsp_t bsp = {
                .width = sz, .height = sz,
                .seed = poi->seed,
                .write = rlk_wg_bsp_write_wallfloor,
                .write_data = &wf,
                .grid = bsp_grid,
                .nodes = bsp_nodes,
            };
            rlk_wg_bsp_init(&bsp);
            rlk_wg_bsp_fill(&bsp, ox, oy, &tm);
        }
    }

    /* Output. */
    printf("=== Fantasy World (POI-driven) ===\n");
    printf("Seed: 0x%llX  Map: %dx%d  Grid: %dx%d cells x %d tiles\n",
           (unsigned long long)nf.seed,
           MAP_W, MAP_H, GRID_W, GRID_H, GRID_CELL);
    printf("POIs: %d total  (%d caves, %d dungeons)\n\n",
           pois.count,
           rlk_wg_poi_count(&pois, RLK_WG_POI_CAVE),
           rlk_wg_poi_count(&pois, RLK_WG_POI_DUNGEON));

    display(&tm);

    printf("\nLegend:\n");
    printf("  ~ water     . plain     T forest    ^ mountain\n");
    printf("  # cave/dungeon wall   ' ' cave floor   , dungeon floor\n");

    return 0;
}
