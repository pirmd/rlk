/*
 * World map demo (terminal): tilemap_gen map + local POI content.
 *
 * Shows the rlk worldgen pipeline headless on one seed:
 *   1. tilemap_gen generates the overworld (terrain, rivers, POIs)
 *   2. each cave/dungeon POI gets local content (rlk_ca_fill / rlk_bsp_fill)
 *      over its bbox; already-processed POIs are reserved (no overlap)
 *   3. map stats + ASCII render
 *
 * Deliberately NOT here: roads between POIs, POI networks, spawn-driven
 * composition — that is game logic (a pirr-style world layer, see the
 * wg_network backlog item), not an rlk demo. Pathfinding is demoed in
 * demo_viewer (nav source) and exercised headless by test_tilemap_gen
 * (playability probe). Stage/field probes: demo_viewer modes/overlays.
 * Raster export for GIS tools: demo_gis.
 *
 * Terminal-only: no PXL dependency.
 *
 *   demo_world [seed]
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "rlk.h"
#include "tilemap_gen.h"

#define SEED 42ULL

/* Largest POI bbox is the dungeon: 32x32 tiles. */
#define MAX_POI_SZ 32

/* --- Local tile types (on top of tilemap_gen's) -------------------------- */

enum {
    TILE_CAVE_WALL = TILE_COUNT,   /* local types start after tilemap_gen's (HOUSE/PLAZA/DOCK included) */
    TILE_CAVE_FLOOR,
    TILE_DUNGEON_WALL,
    TILE_DUNGEON_FLOOR,
    TILE_DEMO_COUNT
};

static const char TILE_CHARS[TILE_DEMO_COUNT] = {
    [TILE_VOID]         = ' ',
    [TILE_GRASS]        = '.',
    [TILE_WATER]         = '~',
    [TILE_RIVER]         = 'r',
    [TILE_LAKE]          = 'l',
    [TILE_ROCK]         = '^',
    [TILE_SAND]         = ':',
    [TILE_TREE]         = 'T',
    [TILE_SNOW]         = '*',
    [TILE_MARSH]        = '%',
    [TILE_FLOOR]        = ',',
    [TILE_PINE]         = 'P',
    [TILE_HOUSE]        = 'H',
    [TILE_PLAZA]        = 'o',
    [TILE_DOCK]         = 'w',
    [TILE_SINKHOLE]     = 'O',
    [TILE_CAVE_WALL]    = '#',
    [TILE_CAVE_FLOOR]   = 'c',
    [TILE_DUNGEON_WALL] = 'D',
    [TILE_DUNGEON_FLOOR]= 'd',
};

/* --- Local POI char (anchors only; ca/bsp entrances are not re-marked) ---- */

static char
poi_char(const rlk_poi_t *p) {
    switch (p->type) {
    case POI_CAVE:       return 'C';
    case POI_DUNGEON:    return 'D';
    case POI_VILLAGE:    return 'V';
    case POI_TOWER:      return 'T';
    case POI_HAMLET:     return 'h';
    case POI_MINE:       return 'M';
    case POI_MILL:       return 'W';
    case POI_PORT:       return 'P';
    case POI_LIGHTHOUSE: return 'L';
    case POI_FORT:       return 'F';
    default:             return '?';
    }
}

/* Count POIs of `type` (user-land helper: the list is POD, count inline). */
static int
poi_count(const rlk_poi_list_t *l, uint8_t type) {
    int n = 0;
    for (int i = 0; i < l->n; i++)
        if (l->pois[(size_t)i].type == type)
            n++;
    return n;
}

/* Fill window over the POI bbox clamped to the map (cellsize 1, in tiles);
   1 and *out filled if non-empty, else 0. Same idiom as gen_post.c. */
static int
poi_window(const rlk_poi_t *p, rlk_rect_t map, rlk_lattice_t *out) {
    rlk_rect_t box;
    if (!rlk_rect_intersect(&box, &p->bbox, &map))
        return 0;
    *out = (rlk_lattice_t){ .w = box.w, .h = box.h, .cellsize = 1,
                           .origin_x = box.x, .origin_y = box.y };
    return 1;
}

/* --- DISPLAY ------------------------------------------------------------- */

/* Render the tilemap and POI anchors as ASCII. */
static void
display(const tilemap_gen_t *gen) {
    static char scr[TM_W * TM_H];
    const rlk_tilemap_t *tm = &gen->tm;
    for (int y = 0; y < tm->h; y++) {
        for (int x = 0; x < tm->w; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(tm, x, y);
            int ti = (t->type < TILE_DEMO_COUNT) ? t->type : TILE_VOID;
            scr[(size_t)y * (size_t)tm->w + (size_t)x] = TILE_CHARS[ti];
        }
    }
    for (int i = 0; i < gen->pois.n; i++) {
        const rlk_poi_t *p = &gen->pois.pois[i];
        if (p->x >= 0 && p->x < tm->w && p->y >= 0 && p->y < tm->h)
            scr[(size_t)p->y * (size_t)tm->w + (size_t)p->x] = poi_char(p);
    }
    for (int y = 0; y < tm->h; y++) {
        fwrite(&scr[(size_t)y * (size_t)tm->w], 1, (size_t)tm->w, stdout);
        putchar('\n');
    }
}

/* --- MAIN ---------------------------------------------------------------- */

int
main(int argc, char **argv) {
    /* usage: demo_world [seed] */
    if (argc > 2) {
        fprintf(stderr, "usage: %s [seed]\n", argv[0]);
        return 2;
    }
    uint64_t seed = (argc > 1) ? strtoull(argv[1], NULL, 0) : SEED;

    /* Static buffers: no allocation. */
    static tilemap_gen_t gen;
    static uint8_t stat_scratch[TM_W * TM_H];
    static uint8_t ca_grid[MAX_POI_SZ * MAX_POI_SZ];
    static int ca_regions[MAX_POI_SZ * MAX_POI_SZ];
    static uint8_t bsp_grid[MAX_POI_SZ * MAX_POI_SZ];
    static rlk_bsp_node_t bsp_nodes[RLK_BSP_MAX_NODES];
    static rlk_poi_t done_buf[256];

    static const demo_wallfloor_t wf_ca = {
        .wall_type = TILE_CAVE_WALL, .floor_type = TILE_CAVE_FLOOR,
    };
    static const demo_wallfloor_t wf_bsp = {
        .wall_type = TILE_DUNGEON_WALL, .floor_type = TILE_DUNGEON_FLOOR,
    };

    /* 1. Overworld. */
    tilemap_gen_generate(&gen, seed);

    /* 1b. Map probes: stats right after generation (deterministic per seed). */
    tilemap_gen_stats_t stats;
    tilemap_gen_stats(&gen, &stats, stat_scratch);
    tilemap_gen_stats_print(&gen, &stats);
    printf("\n");

    /* 2. Local content per cave/dungeon POI. Processed POIs are reserved so
       later bboxes write nothing inside them (overlap rule of poi.h). */
    rlk_poi_list_t done = { done_buf, 0, 256 };
    int n_caves = 0, n_dungeons = 0, n_skipped = 0;
    for (int i = 0; i < gen.pois.n; i++) {
        const rlk_poi_t *p = &gen.pois.pois[i];
        if (p->type != POI_CAVE && p->type != POI_DUNGEON) {
            rlk_poi_push(&done, p);   /* surface sites: reserve only */
            continue;
        }

        /* Window over the POI bbox, clamped to the map (fill requires the
           tilemap to fully cover the window). */
        rlk_lattice_t win;
        if (!poi_window(p, (rlk_rect_t){ 0, 0, TM_W, TM_H }, &win)) {
            n_skipped++;
            rlk_poi_push(&done, p);
            continue;
        }

        if (p->type == POI_CAVE) {
            rlk_ca_t ca = {
                .seed = p->seed,
                .fill_prob = 0.45f,
                .iterations = 5,
                .connect = 1,
                .write = demo_write_wallfloor,
                .write_data = &wf_ca,
                .grid = ca_grid,
                .region_ids = ca_regions,
            };
            rlk_ca_init(&ca);
            rlk_ca_fill(&ca, &win, &gen.tm, NULL, &done, NULL);
            n_caves++;
        } else {
            rlk_bsp_t bsp = {
                .seed = p->seed,
                .write = demo_write_wallfloor,
                .write_data = &wf_bsp,
                .grid = bsp_grid,
                .nodes = bsp_nodes,
            };
            rlk_bsp_init(&bsp);
            rlk_bsp_fill(&bsp, &win, &gen.tm, NULL, &done, NULL);
            n_dungeons++;
        }
        rlk_poi_push(&done, p);
    }

    /* Output. */
    printf("\n=== World map (tilemap_gen + local POI content) ===\n");
    printf("Seed: %llu  Map: %dx%d\n", (unsigned long long)seed, TM_W, TM_H);
    printf("POIs: %d total (cave %d, dungeon %d, village %d, tower %d, hamlet %d, "
           "mine %d, mill %d, port %d, lighthouse %d, fort %d)\n",
           gen.pois.n,
           poi_count(&gen.pois, POI_CAVE),
           poi_count(&gen.pois, POI_DUNGEON),
           poi_count(&gen.pois, POI_VILLAGE),
           poi_count(&gen.pois, POI_TOWER),
           poi_count(&gen.pois, POI_HAMLET),
           poi_count(&gen.pois, POI_MINE),
           poi_count(&gen.pois, POI_MILL),
           poi_count(&gen.pois, POI_PORT),
           poi_count(&gen.pois, POI_LIGHTHOUSE),
           poi_count(&gen.pois, POI_FORT));
    printf("Generated: %d caves, %d dungeons (%d skipped)\n", n_caves, n_dungeons, n_skipped);
    int n_sink = 0;
    for (int y = 0; y < TM_H; y++)
        for (int x = 0; x < TM_W; x++)
            if (rlk_tilemap_get(&gen.tm, x, y)->type == TILE_SINKHOLE)
                n_sink++;
    printf("Sinkholes: %d\n\n", n_sink);

    display(&gen);

    printf("\nLegend:\n");
    printf("  ~ water  r river  l lake  ^ rock  : sand  * snow  %% marsh\n");
    printf("  . grass  , floor  T tree  P pine  H house  o plaza  w dock  O sinkhole\n");
    printf("  # cave wall  c cave floor  D dungeon wall  d dungeon floor\n");
    printf("  C/D/V/T/h/M/W/P/L/F POI anchors\n");

    return 0;
}
