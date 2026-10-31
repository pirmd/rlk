/*
 * World composition demo (level 4/5): POI -> local generators -> roads.
 *
 * Shows the full world pipeline on top of tilemap_gen:
 *   1. tilemap_gen generates the overworld (terrain, rivers, POIs)
 *   2. each cave/dungeon POI gets local content (rlk_ca_fill / rlk_bsp_fill)
 *      over its bbox; already-processed POIs are reserved (no overlap);
 *      each generator emits a spawn entrance POI (gen + 1) that grows the
 *      queue as a marker only (gen > MAX_GEN is not generated)
 *   3. roads connect surface sites (villages, towers) as a minimum spanning
 *      tree (Prim over squared distances), outposts link to the nearest site:
 *      walkability mask + slope-weighted cost table + rlk_path_find/draw
 *
 * Terminal-only: no PXL dependency. Prints an ASCII map plus map stats;
 * argv selects the view: demo_world [seed] [map|elev|rel|flow|lake|river].
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ASCII ramp for float fields, 10 levels. */
static const char RAMP[] = " .:-=+*#%@";

static char
ramp_char(float v) {
    int i = (int)(v * 10.0f);
    if (i < 0) i = 0;
    if (i > 9) i = 9;
    return RAMP[i];
}

/* Display modes: map (tiles+POI) or a generation stage / field probe. */
enum {
    M_MAP = 0,
    M_ELEV,    /* coarse elevation grid */
    M_REL,     /* final per-tile elevation (wg_relief) */
    M_FLOW,    /* drain flow accumulation */
    M_LAKE,    /* lake depth */
    M_RIVER,   /* river mask */
    M_COUNT
};

static void
display(const tilemap_gen_t *gen, int mode) {
    static char scr[TM_W * TM_H];
    const rlk_tilemap_t *tm = &gen->tm;
    for (int y = 0; y < tm->h; y++) {
        for (int x = 0; x < tm->w; x++) {
            size_t idx = (size_t)y * (size_t)tm->w + (size_t)x;
            char c = ' ';
            switch (mode) {
            case M_ELEV:
                c = ramp_char(rlk_grid_sample(&gen->elev_grid, (float)x + 0.5f, (float)y + 0.5f));
                break;
            case M_REL:
                c = ramp_char(gen->final_elev[idx]);
                break;
            case M_FLOW: {
                int ci = 0, cj = 0;
                uint32_t acc = 0;
                if (rlk_grid_cell_of(&gen->elev_grid, x, y, &ci, &cj))
                    acc = gen->drain.acc[(size_t)cj * (size_t)gen->drain.lattice.w + (size_t)ci];
                c = ramp_char((float)acc / ((float)acc + 200.0f));
                break;
            }
            case M_LAKE: {
                int ci = 0, cj = 0;
                float lk = 0.0f;
                if (rlk_grid_cell_of(&gen->elev_grid, x, y, &ci, &cj))
                    lk = gen->lake_buf[(size_t)cj * (size_t)GRID_W + (size_t)ci];
                c = ramp_char(lk / 0.10f);
                break;
            }
            case M_RIVER:
                c = gen->river_mask_buf[idx] > 0.5f ? '#'
                    : (gen->river_carve_buf[idx] > 0.0f ? '.' : ' ');
                break;
            case M_MAP:
            default: {
                const rlk_tile_t *t = rlk_tilemap_get(tm, x, y);
                int ti = (t->type < TILE_DEMO_COUNT) ? t->type : TILE_VOID;
                c = TILE_CHARS[ti];
                break;
            }
            }
            scr[idx] = c;
        }
    }
    if (mode == M_MAP) {
        for (int i = 0; i < gen->pois.n; i++) {
            const rlk_poi_t *p = &gen->pois.v[i];
            if (p->x >= 0 && p->x < tm->w && p->y >= 0 && p->y < tm->h)
                scr[(size_t)p->y * (size_t)tm->w + (size_t)p->x] = poi_char(p);
        }
    }
    for (int y = 0; y < tm->h; y++) {
        fwrite(&scr[(size_t)y * (size_t)tm->w], 1, (size_t)tm->w, stdout);
        putchar('\n');
    }
}

/* Squared euclidean distance between two POI anchors (MST edge weight). */
static int
poi_d2(const rlk_poi_t *pa, const rlk_poi_t *pb) {
    int dx = pb->x - pa->x, dy = pb->y - pa->y;
    return dx * dx + dy * dy;
}

/* Draw one road between two POI anchors over the cost table.
   Returns the path length, or -1 if no path was found. */
static int
road_draw(const rlk_path_cost_t *cost, rlk_path_scratch_t *scr,
          rlk_tilemap_t *tm, int *path_buf, int cap,
          const rlk_poi_t *pa, const rlk_poi_t *pb) {
    int n = 0;
    rlk_err_t err = rlk_path_find(cost, scr, pa->x, pa->y, pb->x, pb->y,
                                  path_buf, cap, &n);
    if (err != RLK_OK) {
        printf("Road (%d,%d) -> (%d,%d): %s\n",
               pa->x, pa->y, pb->x, pb->y, rlk_err_to_string(err));
        return -1;
    }
    rlk_path_draw(tm, path_buf, n, 1, (rlk_tile_t){ .type = TILE_ROAD });
    printf("Road (%d,%d) -> (%d,%d): %d steps\n", pa->x, pa->y, pb->x, pb->y, n);
    return n;
}

/* --- MAIN ---------------------------------------------------------------- */

int
main(int argc, char **argv) {
    /* usage: demo_world [seed] [map|elev|rel|flow|lake|river] */
    uint64_t seed = (argc > 1) ? strtoull(argv[1], NULL, 0) : SEED;
    const char *mode_arg = (argc > 2) ? argv[2] : "map";
    int mode = M_MAP;
    if (!strcmp(mode_arg, "elev"))  mode = M_ELEV;
    else if (!strcmp(mode_arg, "rel"))   mode = M_REL;
    else if (!strcmp(mode_arg, "flow"))  mode = M_FLOW;
    else if (!strcmp(mode_arg, "lake"))  mode = M_LAKE;
    else if (!strcmp(mode_arg, "river")) mode = M_RIVER;
    else if (strcmp(mode_arg, "map")) {
        fprintf(stderr, "usage: %s [seed] [map|elev|rel|flow|lake|river]\n", argv[0]);
        return 2;
    }

    /* Static buffers: no allocation. */
    static tilemap_gen_t gen;
    static uint8_t stat_scratch[TM_W * TM_H];
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
    static uint8_t tree_in[256];   /* Prim MST: in-tree flags */
    static int tree_d2[256];       /* Prim MST: best edge weight per site */
    static int tree_from[256];     /* Prim MST: parent site per site */

    static const rlk_wallfloor_t wf_ca = {
        .wall_type = TILE_CAVE_WALL, .floor_type = TILE_CAVE_FLOOR,
    };
    static const rlk_wallfloor_t wf_bsp = {
        .wall_type = TILE_DUNGEON_WALL, .floor_type = TILE_DUNGEON_FLOOR,
    };

    /* 1. Overworld. */
    tilemap_gen_generate(&gen, seed);

    /* 1b. Map probes: stats right after generation (deterministic per seed). */
    tilemap_gen_stats_t stats;
    tilemap_gen_stats(&gen, &stats, stat_scratch);
    tilemap_gen_stats_print(&gen, &stats);
    printf("\n");

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
        rlk_lattice_t win;
        if (!rlk_poi_window(p, (rlk_rect_t){ 0, 0, TM_W, TM_H }, &win)) {
            n_skipped++;
            rlk_poi_push(&done, p);
            continue;
        }

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
                .write = rlk_write_wallfloor,
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
                .write = rlk_write_wallfloor,
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

    /* 3. Roads: MST over surface sites (villages, towers, hamlets, forts):
       walkability mask + slope cost, then Prim (see below). */
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

    /* Minimum spanning tree (Prim) over the surface sites — specialist review
       2026-10-30, game designer: nearest-neighbor pairing gives a graph with
       dead ends and no hierarchy; an MST connects every site with minimal
       total length. Edge weight = squared euclidean distance (topology is
       geometric, the path itself is slope-weighted). O(n^2), n <= 256, no
       allocation; deterministic (strict < keeps the lowest index on ties). */
    int n_routes = 0, total_len = 0;
    if (nsites > 0) {
        tree_in[0] = 1;
        for (int b = 0; b < nsites; b++) {
            tree_d2[b] = poi_d2(&gen.pois.v[sites[0]], &gen.pois.v[sites[b]]);
            tree_from[b] = 0;
        }
        for (int e = 1; e < nsites; e++) {
            int best = -1, best_d2 = INT_MAX;
            for (int b = 0; b < nsites; b++)
                if (!tree_in[b] && tree_d2[b] < best_d2) { best_d2 = tree_d2[b]; best = b; }
            if (best < 0) break;   /* unreachable remainder (complete graph: cannot happen) */
            tree_in[best] = 1;
            const rlk_poi_t *pa = &gen.pois.v[sites[tree_from[best]]];
            const rlk_poi_t *pb = &gen.pois.v[sites[best]];
            int n = road_draw(&cost, &scr, &gen.tm, path_buf, TM_W * TM_H, pa, pb);
            if (n > 0) { n_routes++; total_len += n; }
            for (int b = 0; b < nsites; b++) {
                if (tree_in[b]) continue;
                int d = poi_d2(pb, &gen.pois.v[sites[b]]);
                if (d < tree_d2[b]) { tree_d2[b] = d; tree_from[b] = best; }
            }
        }
    }

    /* 3b. POI network: each outpost (mine, mill, port, lighthouse) links to
       its nearest settlement in the tree — commerce routes mine -> village
       -> port (specialist review 2026-10-30, game designer). */
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
            int d2 = poi_d2(p, pb);
            if (d2 < best_d2) { best_d2 = d2; best = b; }
        }
        const rlk_poi_t *pb = &gen.pois.v[sites[best]];
        int n = road_draw(&cost, &scr, &gen.tm, path_buf, TM_W * TM_H, p, pb);
        if (n > 0) { n_routes++; total_len += n; }
    }

    /* Output. */
    printf("\n=== World composition (tilemap_gen + POI loop + roads) ===\n");
    printf("Seed: %llu  Map: %dx%d\n", (unsigned long long)seed, TM_W, TM_H);
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

    printf("\n--- view: %s ---\n", mode_arg);
    display(&gen, mode);

    if (mode != M_MAP) {
        printf("\nLegend (map mode for tiles):\n");
    } else {
        printf("\nLegend:\n");
    }
    printf("  ~ water  r river  l lake  ^ rock  : sand  * snow  %% marsh\n");
    printf("  . grass  , floor  T tree  P pine  = road  H house  o plaza  w dock  O sinkhole\n");
    printf("  # cave wall  c cave floor  D dungeon wall  d dungeon floor\n");
    printf("  C/D/V/T/H(h)/M/W/P/L/F POI anchors   > entrance marker\n");

    return 0;
}
