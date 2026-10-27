/*
 * Dungeon demo: standalone wg_bsp with manual POI markers.
 *
 * Generates a BSP dungeon, then marks special tiles as POIs:
 *   '>' entrance   '$' treasure   'B' boss room
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

/* Find the center of the largest room (leaf with max area). */
static void
find_largest_room(const rlk_wg_bsp_t *bsp, int *cx, int *cy) {
    int best_area = -1;
    int bx = 0, by = 0;
    for (int i = 0; i < bsp->node_count; i++) {
        const rlk_wg_bsp_node_t *n = &bsp->nodes[i];
        /* Leaf nodes have left == -1. */
        if (n->left >= 0) continue;
        int area = n->rw * n->rh;
        if (area > best_area) {
            best_area = area;
            bx = n->rx + n->rw / 2;
            by = n->ry + n->rh / 2;
        }
    }
    *cx = bx;
    *cy = by;
}

/* Find a floor tile near (cx, cy) — for placing POI markers. */
static int
find_floor_near(const rlk_tilemap_t *tm, int cx, int cy,
                int *ox, int *oy) {
    for (int r = 0; r < MAP_W; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx != -r && dx != r && dy != -r && dy != r) continue;
                int x = cx + dx, y = cy + dy;
                rlk_tile_t *t = rlk_tilemap_get(tm, x, y);
                if (t && t->type == T_FLOOR) {
                    *ox = x;
                    *oy = y;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* --- MAIN ---------------------------------------------------------------- */

int
main(void) {
    static rlk_tile_t tile_buf[MAP_W * MAP_H];
    static uint8_t bsp_grid[MAP_W * MAP_H];
    static rlk_wg_bsp_node_t bsp_nodes[RLK_WG_BSP_MAX_NODES];

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP_W, MAP_H, tile_buf, sizeof(tile_buf));

    /* BSP dungeon. */
    static const rlk_wg_bsp_wallfloor_t wf = {
        .wall_type = T_WALL, .floor_type = T_FLOOR,
    };
    rlk_wg_bsp_t bsp = {
        .width = MAP_W,
        .height = MAP_H,
        .seed = 0xD06E0BULL,
        .min_room_size = 5,
        .max_depth = 6,
        .write = rlk_wg_bsp_write_wallfloor,
        .write_data = &wf,
        .grid = bsp_grid,
        .nodes = bsp_nodes,
    };
    rlk_wg_bsp_init(&bsp);
    rlk_wg_bsp_fill(&bsp, 0, 0, &tm);

    /* Place POI markers. */
    int cx, cy;
    find_largest_room(&bsp, &cx, &cy);

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

    /* Boss: center of largest room. */
    rlk_tile_t *boss = rlk_tilemap_get(&tm, cx, cy);
    if (boss && boss->type == T_FLOOR)
        boss->type = T_BOSS;

    /* Treasure: floor tile far from entrance. */
    int tx, ty;
    if (find_floor_near(&tm, cx, cy, &tx, &ty)) {
        /* Search outward from boss for a floor tile that isn't the boss. */
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
    }

    /* Output. */
    printf("=== Dungeon (BSP + POI markers) ===\n");
    printf("Seed: 0x%llX  Size: %dx%d  Rooms: %d  Nodes: %d\n\n",
           (unsigned long long)bsp.seed,
           MAP_W, MAP_H, bsp.room_count, bsp.node_count);

    display(&tm);

    printf("\nLegend:\n");
    printf("  # wall   . floor   > entrance   $ treasure   B boss\n");

    return 0;
}
