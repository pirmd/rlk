/*
 * Cave demo: standalone wg_ca with entrance detection.
 *
 * Generates a cave system, then detects entrances: floor regions
 * that touch the map border. Marks them as POI entrances.
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

/* Detect entrances: floor cells on the map border. */
static int
detect_entrances(rlk_tilemap_t *tm) {
    int count = 0;
    for (int x = 0; x < tm->w; x++) {
        rlk_tile_t *t = rlk_tilemap_get(tm, x, 0);
        if (t && t->type == T_FLOOR) { t->type = T_ENTRANCE; count++; }
        t = rlk_tilemap_get(tm, x, tm->h - 1);
        if (t && t->type == T_FLOOR) { t->type = T_ENTRANCE; count++; }
    }
    for (int y = 0; y < tm->h; y++) {
        rlk_tile_t *t = rlk_tilemap_get(tm, 0, y);
        if (t && t->type == T_FLOOR) { t->type = T_ENTRANCE; count++; }
        t = rlk_tilemap_get(tm, tm->w - 1, y);
        if (t && t->type == T_FLOOR) { t->type = T_ENTRANCE; count++; }
    }
    return count;
}

/* --- MAIN ---------------------------------------------------------------- */

int
main(void) {
    static rlk_tile_t tile_buf[MAP_W * MAP_H];
    static uint8_t ca_grid[MAP_W * MAP_H];
    static int ca_regions[MAP_W * MAP_H];

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP_W, MAP_H, tile_buf, sizeof(tile_buf));

    /* CA cave. */
    static const rlk_wg_ca_wallfloor_t wf = {
        .wall_type = T_WALL, .floor_type = T_FLOOR,
    };
    rlk_wg_ca_t ca = {
        .width = MAP_W,
        .height = MAP_H,
        .seed = 0xCAFE0CULL,
        .fill_prob = 0.47f,
        .birth_low = 4, .birth_high = 5,
        .survive_low = 4, .survive_high = 8,
        .iterations = 5,
        .connect = 1,
        .write = rlk_wg_ca_write_wallfloor,
        .write_data = &wf,
        .grid = ca_grid,
        .region_ids = ca_regions,
    };
    rlk_wg_ca_init(&ca);
    rlk_wg_ca_fill(&ca, 0, 0, &tm);

    /* Mark entrances (floor cells touching the border). */
    int entrances = detect_entrances(&tm);

    /* Output. */
    printf("=== Cave Explorer (CA + entrance POIs) ===\n");
    printf("Seed: 0x%llX  Size: %dx%d  Iterations: %d  Regions: %d  Entrances: %d\n\n",
           (unsigned long long)ca.seed,
           MAP_W, MAP_H, ca.cur_iter, ca.region_count, entrances);

    display(&tm);

    printf("\nLegend:\n");
    printf("  # wall   ' ' floor   > entrance (floor on border)\n");

    return 0;
}
