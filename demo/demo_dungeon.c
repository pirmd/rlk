/*
 * Dungeon demo: standalone bsp with a spawn POI.
 *
 * Generates a BSP dungeon; rlk_bsp_fill reports the largest room as a spawn
 * POI (the boss room). Also marks an entrance ('>') and a treasure ('$').
 *
 * Terminal-only: no PXL dependency. Prints an ASCII map.
 */

#include <stdint.h>
#include <stdio.h>

#include "rlk.h"

/* --- CONSTANTS ----------------------------------------------------------- */
#define MAP_W 60
#define MAP_H 30

/* Tile types for display. */
enum {
    T_WALL = 0,
    T_FLOOR,
    T_ENTRANCE,   /* > */
    T_TREASURE,   /* $ */
    T_BOSS,       /* B */
    T_COUNT
};

static const char TILE_CHARS[T_COUNT] = {
    '#', '.', '>', '$', 'B',
};

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

/* --- MAIN ---------------------------------------------------------------- */

int
main(void) {
    static rlk_tile_t tile_buf[MAP_W * MAP_H];
    static uint8_t bsp_grid[MAP_W * MAP_H];
    static rlk_bsp_node_t bsp_nodes[RLK_BSP_MAX_NODES];
    static float win_buf[MAP_W * MAP_H];

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP_W, MAP_H, tile_buf, sizeof(tile_buf));

    /* Window: the whole map, one cell per tile (g->v is not read by fill). */
    rlk_grid_t win = { .lattice = { .w = MAP_W, .h = MAP_H, .cellsize = 1,
                                    .origin_x = 0, .origin_y = 0 }, .v = win_buf };

    /* BSP dungeon. */
    static const rlk_bsp_wallfloor_t wf = {
        .wall_type = T_WALL, .floor_type = T_FLOOR,
    };
    rlk_bsp_t bsp = {
        .seed = 0xD06E0BULL,
        .min_room_size = 5,
        .max_depth = 6,
        .write = rlk_bsp_write_wallfloor,
        .write_data = &wf,
        .grid = bsp_grid,
        .nodes = bsp_nodes,
    };
    rlk_bsp_init(&bsp);
    rlk_poi_t spawn = { .type = T_BOSS };
    rlk_err_t err = rlk_bsp_fill(&bsp, &win, &tm, NULL, NULL, &spawn);
    if (err != RLK_OK) {
        fprintf(stderr, "bsp_fill: %s\n", rlk_err_to_string(err));
        return 1;
    }

    /* Boss: centre of the largest room (from the spawn POI). */
    int cx = spawn.x, cy = spawn.y;
    rlk_tile_t *boss = rlk_tilemap_get(&tm, cx, cy);
    if (boss && boss->type == T_FLOOR)
        boss->type = T_BOSS;

    /* Entrance: first floor tile found scanning from top-left. */
    int ex = -1, ey = -1;
    for (int y = 0; y < MAP_H && ex < 0; y++) {
        for (int x = 0; x < MAP_W && ex < 0; x++) {
            rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            if (t && t->type == T_FLOOR) {
                ex = x;
                ey = y;
            }
        }
    }
    if (ex >= 0) {
        rlk_tile_t *t = rlk_tilemap_get(&tm, ex, ey);
        if (t) t->type = T_ENTRANCE;
    }

    /* Treasure: first floor tile on the ring around the boss room. */
    for (int r = 1; r < MAP_W; r++) {
        int found = 0;
        for (int dy = -r; dy <= r && !found; dy++) {
            for (int dx = -r; dx <= r && !found; dx++) {
                if (dx != -r && dx != r && dy != -r && dy != r)
                    continue;
                int x = cx + dx, y = cy + dy;
                rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
                if (t && t->type == T_FLOOR) {
                    t->type = T_TREASURE;
                    found = 1;
                }
            }
        }
        if (found) break;
    }

    /* Output. */
    printf("=== Dungeon (BSP + spawn POI) ===\n");
    printf("Seed: 0x%llX  Size: %dx%d  Rooms: %d  Nodes: %d  Boss room: (%d,%d)\n\n",
           (unsigned long long)bsp.seed,
           MAP_W, MAP_H, bsp.room_count, bsp.node_count, cx, cy);

    display(&tm);

    printf("\nLegend:\n");
    printf("  # wall   . floor   > entrance   $ treasure   B boss\n");

    return 0;
}
