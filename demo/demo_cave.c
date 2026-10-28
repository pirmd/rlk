/*
 * Cave demo: standalone ca with an entrance POI.
 *
 * Generates a cave system; rlk_ca_fill reports the entrance (centre of the
 * largest floor region) as a spawn POI.
 *
 * Terminal-only: no PXL dependency. Prints an ASCII map.
 */

#include <stdint.h>
#include <stdio.h>

#include "rlk.h"

/* --- CONSTANTS ----------------------------------------------------------- */
#define MAP_W 80
#define MAP_H 40

/* Tile types for display. */
enum {
    T_WALL = 0,
    T_FLOOR,
    T_ENTRANCE,   /* > */
    T_COUNT
};

static const char TILE_CHARS[T_COUNT] = {
    '#', ' ', '>',
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
    static uint8_t ca_grid[MAP_W * MAP_H];
    static int ca_regions[MAP_W * MAP_H];
    static float win_buf[MAP_W * MAP_H];

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP_W, MAP_H, tile_buf, sizeof(tile_buf));

    /* Window: the whole map, one cell per tile (g->v is not read by fill). */
    rlk_grid_t win = { .lattice = { .w = MAP_W, .h = MAP_H, .cellsize = 1,
                                    .origin_x = 0, .origin_y = 0 }, .v = win_buf };

    /* CA cave. */
    static const rlk_ca_wallfloor_t wf = {
        .wall_type = T_WALL, .floor_type = T_FLOOR,
    };
    rlk_ca_t ca = {
        .seed = 0xCAFE0CULL,
        .fill_prob = 0.47f,
        .birth_low = 4, .birth_high = 5,
        .survive_low = 4, .survive_high = 8,
        .iterations = 5,
        .connect = 1,
        .write = rlk_ca_write_wallfloor,
        .write_data = &wf,
        .grid = ca_grid,
        .region_ids = ca_regions,
    };
    rlk_ca_init(&ca);
    rlk_poi_t entrance = { .type = T_ENTRANCE };
    rlk_err_t err = rlk_ca_fill(&ca, &win, &tm, NULL, NULL, &entrance);
    if (err != RLK_OK) {
        fprintf(stderr, "ca_fill: %s\n", rlk_err_to_string(err));
        return 1;
    }

    /* Mark the entrance (centre of the largest floor region). */
    int marked = 0;
    rlk_tile_t *t = rlk_tilemap_get(&tm, entrance.x, entrance.y);
    if (t && t->type == T_FLOOR) {
        t->type = T_ENTRANCE;
        marked = 1;
    }

    /* Output. */
    printf("=== Cave Explorer (CA + entrance POI) ===\n");
    printf("Seed: 0x%llX  Size: %dx%d  Iterations: %d  Regions: %d  Entrance: (%d,%d)%s\n\n",
           (unsigned long long)ca.seed,
           MAP_W, MAP_H, ca.cur_iter, ca.region_count,
           entrance.x, entrance.y, marked ? "" : " (not on floor)");

    display(&tm);

    printf("\nLegend:\n");
    printf("  # wall   ' ' floor   > entrance (largest region centre)\n");

    return 0;
}
