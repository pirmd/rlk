/*
 * World composition demo (level 4/5): POI -> local generators -> roads.
 *
 * Shows the full world pipeline on top of tilemap_gen:
 *   1. tilemap_gen generates the overworld (terrain, rivers, POIs)
 *   2. each cave/dungeon POI gets local content (rlk_ca_fill / rlk_bsp_fill)
 *      over its bbox; already-processed POIs are reserved (no overlap);
 *      each generator emits a spawn entrance POI (gen + 1) that grows the
 *      queue as a marker only (gen > MAX_GEN is not generated)
 *   3. roads connect surface sites (villages, towers): walkability mask +
 *      slope-weighted cost table + rlk_path_find + rlk_path_draw
 *
 * Terminal-only: no PXL dependency. Prints an ASCII map.
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "rlk.h"
#include "tilemap_gen.h"

#define SEED 42ULL

/* POIs with gen > MAX_GEN are markers only (entrances), not generated. */
#define MAX_GEN 0

/* Elevation [0,1] represents 128 tiles of relief (same role as gdaldem -s). */
#define ELEV_ZSCALE 128.0f

/* Largest POI bbox is the dungeon: 32x32 tiles. */
#define MAX_POI_SZ 32

/* --- Local tile types (on top of tilemap_gen's) -------------------------- */

enum {
    TILE_CAVE_WALL = TILE_COUNT,   /* local types start after tilemap_gen's (HOUSE/PLAZA/DOCK included) */
    TILE_CAVE_FLOOR,
    TILE_DUNGEON_WALL,
    TILE_DUNGEON_FLOOR,
    TILE_ROAD,
    TILE_DEMO_COUNT
};

static const char TILE_CHARS[TILE_DEMO_COUNT] = {
    [TILE_VOID]         = ' ',
    [TILE_GRASS]        = '.',
    [TILE_WATER]        = '~',
    [TILE_RIVER]        = 'r',
    [TILE_LAKE]         = 'l',
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
    [TILE_ROAD]         = '=',
};

/* Walkability: water and generated site interiors are not walkable, so
   roads avoid rivers and POI areas. */
static const uint8_t TILE_FLAGS[TILE_DEMO_COUNT] = {
    [TILE_VOID]          = 0,
    [TILE_GRASS]         = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_WATER]         = RLK_TILE_TRANSPARENT,
    [TILE_RIVER]         = RLK_TILE_TRANSPARENT,
    [TILE_LAKE]          = RLK_TILE_TRANSPARENT,
    [TILE_ROCK]          = 0,
    [TILE_SAND]          = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_TREE]          = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_SNOW]          = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_MARSH]         = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_FLOOR]         = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_PINE]          = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_HOUSE]         = 0,
    [TILE_PLAZA]         = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_DOCK]          = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_SINKHOLE]      = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_CAVE_WALL]     = 0,
    [TILE_CAVE_FLOOR]    = RLK_TILE_TRANSPARENT,
    [TILE_DUNGEON_WALL]  = 0,
    [TILE_DUNGEON_FLOOR] = RLK_TILE_TRANSPARENT,
    [TILE_ROAD]          = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
};

static const rlk_tile_props_t TILE_PROPS = { .flags = TILE_FLAGS, .ntypes = TILE_DEMO_COUNT };

/* --- Local POI types (entrance markers emitted by ca/bsp) ---------------- */

enum {
    POI_CAVE_ENTR = POI_KIND_COUNT,
    POI_DUNGEON_ENTR,
};

static char
poi_char(const rlk_poi_t *p) {
    if (p->gen > MAX_GEN)
        return '>';
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

/* --- DISPLAY ------------------------------------------------------------- */

static void
display(const rlk_tilemap_t *tm, const rlk_poi_list_t *pois) {
    static char scr[TM_W * TM_H];
    for (int y = 0; y < tm->h; y++)
        for (int x = 0; x < tm->w; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(tm, x, y);
            int idx = (t->type < TILE_DEMO_COUNT) ? t->type : TILE_VOID;
            scr[(size_t)y * (size_t)tm->w + (size_t)x] = TILE_CHARS[idx];
        }
    for (int i = 0; i < pois->n; i++) {
        const rlk_poi_t *p = &pois->v[i];
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
main(void) {
    /* Static buffers: no allocation. */
    static tilemap_gen_t gen;
    static float win_buf[MAX_POI_SZ * MAX_POI_SZ];
    static uint8_t ca_grid[MAX_POI_SZ * MAX_POI_SZ];
    static int ca_regions[MAX_POI_SZ * MAX_POI_SZ];
    static uint8_t bsp_grid[MAX_POI_SZ * MAX_POI_SZ];
    static rlk_bsp_node_t bsp_nodes[RLK_BSP_MAX_NODES];
    static rlk_poi_t done_buf[256];
    static uint8_t walk_buf[TM_W * TM_H];
    static uint8_t cost_buf[TM_W * TM_H];
    static int p_g[TM_W * TM_H];
    static int p_parent[TM_W * TM_H];
    static int p_heap[TM_W * TM_H];
    static int p_heap_pos[TM_W * TM_H];
    static uint8_t p_closed[TM_W * TM_H];
    static int path_buf[2 * TM_W * TM_H];
    static int sites[256];

    static const rlk_ca_wallfloor_t wf_ca = {
        .wall_type = TILE_CAVE_WALL, .floor_type = TILE_CAVE_FLOOR,
    };
    static const rlk_bsp_wallfloor_t wf_bsp = {
        .wall_type = TILE_DUNGEON_WALL, .floor_type = TILE_DUNGEON_FLOOR,
    };

    /* 1. Overworld. */
    tilemap_gen_generate(&gen, SEED);

    /* 2. Local generation per POI. Processed POIs are reserved so later
       bboxes write nothing inside them (overlap rule of poi.h). Spawn
       entrances are pushed back into the queue (gen + 1): the loop picks
       them up but only reserves them. */
    rlk_poi_list_t done = { done_buf, 0, 256 };
    int n_caves = 0, n_dungeons = 0, n_skipped = 0;
    for (int i = 0; i < gen.pois.n; i++) {
        const rlk_poi_t *p = &gen.pois.v[i];
        int is_site = (p->type == POI_CAVE || p->type == POI_DUNGEON);
        if (p->gen > MAX_GEN || !is_site) {
            rlk_poi_push(&done, p);   /* surface sites and markers: reserve only */
            continue;
        }

        /* Window over the POI bbox, clamped to the map (fill requires the
           tilemap to fully cover the window). */
        rlk_rect_t map_r = { 0, 0, TM_W, TM_H };
        rlk_rect_t box;
        if (!rlk_rect_intersect(&box, &p->bbox, &map_r)) {
            n_skipped++;
            rlk_poi_push(&done, p);
            continue;
        }
        rlk_grid_t win = { .lattice = { .w = box.w, .h = box.h, .cellsize = 1,
                                        .origin_x = box.x, .origin_y = box.y },
                           .v = win_buf };

        rlk_poi_t spawn = {
            .type = (p->type == POI_CAVE) ? POI_CAVE_ENTR : POI_DUNGEON_ENTR,
            .gen = (uint8_t)(p->gen + 1),
            .level = p->level,
        };

        rlk_err_t err;
        if (p->type == POI_CAVE) {
            rlk_ca_t ca = {
                .seed = p->seed,
                .fill_prob = 0.45f,
                .iterations = 5,
                .connect = 1,
                .write = rlk_ca_write_wallfloor,
                .write_data = &wf_ca,
                .grid = ca_grid,
                .region_ids = ca_regions,
            };
            rlk_ca_init(&ca);
            err = rlk_ca_fill(&ca, &win, &gen.tm, NULL, &done, &spawn);
            if (err == RLK_OK) n_caves++;
        } else {
            rlk_bsp_t bsp = {
                .seed = p->seed,
                .write = rlk_bsp_write_wallfloor,
                .write_data = &wf_bsp,
                .grid = bsp_grid,
                .nodes = bsp_nodes,
            };
            rlk_bsp_init(&bsp);
            err = rlk_bsp_fill(&bsp, &win, &gen.tm, NULL, &done, &spawn);
            if (err == RLK_OK) n_dungeons++;
        }
        if (err != RLK_OK) {
            fprintf(stderr, "fill POI %d (type %u): %s\n",
                    i, p->type, rlk_err_to_string(err));
            n_skipped++;
        } else if (rlk_poi_push(&gen.pois, &spawn) != RLK_OK) {
            n_skipped++;   /* queue full: entrance marker dropped */
        }
        rlk_poi_push(&done, p);
    }

    /* 3. Roads between surface sites (villages, towers): mask + slope cost. */
    rlk_mask_t walkable = { .lattice = rlk_tilemap_lattice(&gen.tm), .v = walk_buf };
    rlk_mask_from_tilemap(&walkable, &gen.tm, &TILE_PROPS, RLK_TILE_WALKABLE, NULL);

    rlk_lattice_t tl = rlk_tilemap_lattice(&gen.tm);
    for (int y = 0; y < tl.h; y++) {
        for (int x = 0; x < tl.w; x++) {
            int idx = y * tl.w + x;
            if (!rlk_mask_allows(&walkable, x, y)) {
                cost_buf[idx] = RLK_PATH_BLOCKED;
                continue;
            }
            int ci, cj;
            float s = 0.0f;
            if (rlk_grid_cell_of(&gen.elev_grid, x, y, &ci, &cj))
                s = rlk_slope_at(&gen.elev_grid, ci, cj, ELEV_ZSCALE);
            cost_buf[idx] = (uint8_t)rlk_clampf(1.0f + 20.0f * s, 1.0f, 255.0f);
        }
    }
    rlk_path_cost_t cost = { .lattice = tl, .table = cost_buf };
    rlk_path_scratch_t scr = { p_g, p_parent, p_heap, p_heap_pos, p_closed };

    int nsites = 0;
    for (int i = 0; i < gen.pois.n; i++) {
        const rlk_poi_t *p = &gen.pois.v[i];
        if (p->gen != 0) continue;
        switch (p->type) {
        case POI_VILLAGE: case POI_TOWER: case POI_HAMLET: case POI_FORT:
            sites[nsites++] = i;
            break;
        default:
            break;
        }
    }

    int n_routes = 0, total_len = 0;
    for (int a = 0; a < nsites; a++) {
        const rlk_poi_t *pa = &gen.pois.v[sites[a]];
        int best = -1, best_d2 = INT_MAX;
        for (int b = 0; b < nsites; b++) {
            if (b == a) continue;
            const rlk_poi_t *pb = &gen.pois.v[sites[b]];
            int dx = pb->x - pa->x, dy = pb->y - pa->y;
            int d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best = b; }
        }
        if (best < 0 || sites[best] < sites[a]) continue;   /* each pair once */
        const rlk_poi_t *pb = &gen.pois.v[sites[best]];

        int n = 0;
        rlk_err_t err = rlk_path_find(&cost, &scr, pa->x, pa->y, pb->x, pb->y,
                                      path_buf, TM_W * TM_H, &n);
        if (err == RLK_OK) {
            rlk_path_draw(&gen.tm, path_buf, n, 1, (rlk_tile_t){ .type = TILE_ROAD });
            n_routes++;
            total_len += n;
            printf("Road (%d,%d) -> (%d,%d): %d steps\n", pa->x, pa->y, pb->x, pb->y, n);
        } else {
            printf("Road (%d,%d) -> (%d,%d): %s\n",
                   pa->x, pa->y, pb->x, pb->y, rlk_err_to_string(err));
        }
    }

    /* 3b. POI network: each outpost (mine, mill, port, lighthouse) links to
       its nearest settlement — commerce routes mine -> village -> port. */
    for (int i = 0; i < gen.pois.n && nsites > 0; i++) {
        const rlk_poi_t *p = &gen.pois.v[i];
        if (p->gen != 0)
            continue;
        switch (p->type) {
        case POI_MINE: case POI_MILL: case POI_PORT: case POI_LIGHTHOUSE:
            break;
        default:
            continue;
        }
        int best = -1, best_d2 = INT_MAX;
        for (int b = 0; b < nsites; b++) {
            const rlk_poi_t *pb = &gen.pois.v[sites[b]];
            int dx = pb->x - p->x, dy = pb->y - p->y;
            int d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best = b; }
        }
        const rlk_poi_t *pb = &gen.pois.v[sites[best]];
        int n = 0;
        rlk_err_t err = rlk_path_find(&cost, &scr, p->x, p->y, pb->x, pb->y,
                                      path_buf, TM_W * TM_H, &n);
        if (err == RLK_OK) {
            rlk_path_draw(&gen.tm, path_buf, n, 1, (rlk_tile_t){ .type = TILE_ROAD });
            n_routes++;
            total_len += n;
            printf("Road (%d,%d) -> (%d,%d): %d steps\n", p->x, p->y, pb->x, pb->y, n);
        } else {
            printf("Road (%d,%d) -> (%d,%d): %s\n",
                   p->x, p->y, pb->x, pb->y, rlk_err_to_string(err));
        }
    }

    /* Output. */
    printf("\n=== World composition (tilemap_gen + POI loop + roads) ===\n");
    printf("Seed: %llu  Map: %dx%d\n", (unsigned long long)SEED, TM_W, TM_H);
    printf("POIs: %d total (cave %d, dungeon %d, village %d, tower %d, hamlet %d, "
           "mine %d, mill %d, port %d, lighthouse %d, fort %d, entrances %d)\n",
           gen.pois.n,
           rlk_poi_count(&gen.pois, POI_CAVE),
           rlk_poi_count(&gen.pois, POI_DUNGEON),
           rlk_poi_count(&gen.pois, POI_VILLAGE),
           rlk_poi_count(&gen.pois, POI_TOWER),
           rlk_poi_count(&gen.pois, POI_HAMLET),
           rlk_poi_count(&gen.pois, POI_MINE),
           rlk_poi_count(&gen.pois, POI_MILL),
           rlk_poi_count(&gen.pois, POI_PORT),
           rlk_poi_count(&gen.pois, POI_LIGHTHOUSE),
           rlk_poi_count(&gen.pois, POI_FORT),
           rlk_poi_count(&gen.pois, POI_CAVE_ENTR) + rlk_poi_count(&gen.pois, POI_DUNGEON_ENTR));
    printf("Generated: %d caves, %d dungeons (%d skipped)\n", n_caves, n_dungeons, n_skipped);
    int n_sink = 0;
    for (int y = 0; y < TM_H; y++)
        for (int x = 0; x < TM_W; x++)
            if (rlk_tilemap_get(&gen.tm, x, y)->type == TILE_SINKHOLE)
                n_sink++;
    printf("Karst sinkholes: %d\n", n_sink);
    printf("Roads: %d routes, %d tiles total\n\n", n_routes, total_len);

    display(&gen.tm, &gen.pois);

    printf("\nLegend:\n");
    printf("  ~ water  r river  l lake  ^ rock  : sand  * snow  %% marsh\n");
    printf("  . grass  , floor  T tree  P pine  = road  H house  o plaza  w dock  O sinkhole\n");
    printf("  # cave wall  c cave floor  D dungeon wall  d dungeon floor\n");
    printf("  C/D/V/T/H(h)/M/W/P/L/F POI anchors   > entrance marker\n");

    return 0;
}
