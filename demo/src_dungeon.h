#ifndef SRC_DUNGEON_H
#define SRC_DUNGEON_H

/*
 * src_dungeon - viewer sources over the standalone generators ca and bsp
 * (demo component).
 *
 * Two sources sharing one state: "ca" (cellular automaton cave with an
 * entrance POI) and "bsp" (BSP dungeon with a spawn POI). One display
 * mode (wall/floor), one overlay (spawn marker). R regenerates with
 * seed + 1.
 *
 *   static src_dungeon_t d;
 *   src_dungeon_init(&d, DUN_SRC_CA, seed);
 *   src_t src = src_dungeon_source(&d);
 *
 * Header-only, POD, caller-owned, no allocation.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "pxl.h"
#include "source.h"
#include "rlk.h"
#include "tilemap_gen.h"   /* demo_write_wallfloor: ready-made ca/bsp writer */

#define DUN_W 80
#define DUN_H 40

/* Tile types */
enum {
    DUN_WALL = 0,
    DUN_FLOOR,
    DUN_COUNT
};

/* Source kind */
enum {
    DUN_SRC_CA = 0,
    DUN_SRC_BSP
};

/* Overlays */
enum {
    DUN_OVL_SPAWN = 0,   /* entrance/spawn POI marker */
    DUN_OVL_COUNT
};

#define DUN_OVL_SPAWN_BIT (1u << DUN_OVL_SPAWN)

static const uint32_t DUN_COLORS[DUN_COUNT] = {
    0xFF303030U, /* wall: dark gray */
    0xFFB0B0B0U, /* floor: light gray */
};

static const char *const DUN_NAMES[DUN_COUNT] = { "Wall", "Floor" };

/* --- STATE --------------------------------------------------------------- */

typedef struct {
    rlk_tilemap_t tm;
    rlk_tile_t tiles[DUN_W * DUN_H];
    uint8_t grid[DUN_W * DUN_H];          /* ca/bsp scratch grid */
    int regions[DUN_W * DUN_H];           /* ca region ids */
    rlk_bsp_node_t nodes[RLK_BSP_MAX_NODES];
    rlk_poi_t spawn;                      /* entrance / spawn POI */
    uint64_t seed;
    int kind;                             /* DUN_SRC_CA / DUN_SRC_BSP */
    int region_count;                     /* ca regions / bsp rooms */
} src_dungeon_t;

static inline void
src_dungeon_build(src_dungeon_t *d) {
    assert(d);

    rlk_tilemap_init(&d->tm, DUN_W, DUN_H, d->tiles, sizeof(d->tiles));

    /* Window: the whole map, one cell per tile. */
    rlk_lattice_t win = { .w = DUN_W, .h = DUN_H, .cellsize = 1,
                          .origin_x = 0, .origin_y = 0 };

    if (d->kind == DUN_SRC_BSP) {
        static const demo_wallfloor_t wf = { .wall_type = DUN_WALL, .floor_type = DUN_FLOOR };
        rlk_bsp_t bsp = {
            .seed = d->seed,
            .min_room_size = 5,
            .max_depth = 6,
            .write = demo_write_wallfloor,
            .write_data = &wf,
            .grid = d->grid,
            .nodes = d->nodes,
        };
        rlk_bsp_init(&bsp);
        rlk_bsp_fill(&bsp, &win, &d->tm, NULL, NULL, &d->spawn);
        d->region_count = bsp.room_count;
    } else {
        static const demo_wallfloor_t wf = { .wall_type = DUN_WALL, .floor_type = DUN_FLOOR };
        rlk_ca_t ca = {
            .seed = d->seed,
            .fill_prob = 0.47f,
            .birth_low = 4, .birth_high = 5,
            .survive_low = 4, .survive_high = 8,
            .iterations = 5,
            .connect = 1,
            .write = demo_write_wallfloor,
            .write_data = &wf,
            .grid = d->grid,
            .region_ids = d->regions,
        };
        rlk_ca_init(&ca);
        rlk_ca_fill(&ca, &win, &d->tm, NULL, NULL, &d->spawn);
        d->region_count = ca.region_count;
    }
}

static inline void
src_dungeon_init(src_dungeon_t *d, int kind, uint64_t seed) {
    assert(d);
    d->kind = kind;
    d->seed = seed;
    src_dungeon_build(d);
}

/* --- SOURCE CALLBACKS ---------------------------------------------------- */

static inline uint32_t
src_dungeon_color(void *data, int mode, int wx, int wy) {
    (void)mode;
    const src_dungeon_t *d = (const src_dungeon_t *)data;
    const rlk_tile_t *t = rlk_tilemap_get(&d->tm, wx, wy);
    if (!t) return 0xFF101010U;
    return DUN_COLORS[t->type % DUN_COUNT];
}

static inline uint32_t
src_dungeon_overlay(void *data, int o, int wx, int wy, uint32_t base) {
    (void)data; (void)o; (void)wx; (void)wy;
    return base;   /* spawn is drawn by src_dungeon_draw */
}

static inline void
src_dungeon_draw(pxl_canvas_t *cnv, void *data, int o, int mode, const src_view_t *v) {
    assert(cnv && data && v);
    (void)mode;
    const src_dungeon_t *d = (const src_dungeon_t *)data;
    if (o != DUN_OVL_SPAWN)
        return;

    int sx = d->spawn.x - v->wx;
    int sy = d->spawn.y - v->wy;
    if (sx < 0 || sx >= v->vw || sy < 0 || sy >= v->vh)
        return;

    int half = v->tile_px / 2;
    int cx = v->dx + sx * v->tile_px;
    int cy = v->dy + sy * v->tile_px;
    pxl_canvas_set_color(cnv, 0xFFFFA500U);
    pxl_fill_rect(cnv, cx, cy, v->tile_px, v->tile_px);
    pxl_canvas_set_color(cnv, 0xFFFFFFFFU);
    pxl_draw_rect(cnv, cx + half / 2, cy + half / 2, half, half);
}

static inline void
src_dungeon_map_info(char *buf, size_t cap, const void *data) {
    const src_dungeon_t *d = (const src_dungeon_t *)data;
    snprintf(buf, cap,
        "MAP\n\n"
        "Gen  : %s\n"
        "Size : %d x %d\n"
        "%s: %d\n"
        "Seed : %llu\n"
        "Spawn: (%d,%d)\n\n"
        "Tab/Sh+Tab: mode\n"
        "1: overlay  R: renew",
        d->kind == DUN_SRC_BSP ? "BSP" : "CA",
        DUN_W, DUN_H,
        d->kind == DUN_SRC_BSP ? "Rooms" : "Regions", d->region_count,
        (unsigned long long)d->seed,
        d->spawn.x, d->spawn.y);
}

static inline void
src_dungeon_tile_info(char *buf, size_t cap, const void *data, int wx, int wy) {
    const src_dungeon_t *d = (const src_dungeon_t *)data;
    const rlk_tile_t *t = rlk_tilemap_get(&d->tm, wx, wy);
    snprintf(buf, cap,
        "TILE\n\n"
        "Position: (%d,%d)\n"
        "Type    : %s (%d)\n"
        "Walkable: %s\n"
        "Spawn   : %s",
        wx, wy,
        t ? DUN_NAMES[t->type % DUN_COUNT] : "?", t ? t->type : -1,
        (t && t->type == DUN_FLOOR) ? "yes" : "no",
        (wx == d->spawn.x && wy == d->spawn.y) ? "yes" : "no");
}

static inline int
src_dungeon_legend(widget_item_t *items, int cap, const void *data,
                   int mode, unsigned overlays) {
    (void)data; (void)mode;
    int n = 0;
    for (int i = 0; i < DUN_COUNT && n < cap; i++) {
        items[n].color = DUN_COLORS[i];
        items[n].label = DUN_NAMES[i];
        n++;
    }
    if ((overlays & DUN_OVL_SPAWN_BIT) && n < cap) {
        items[n].color = 0xFFFFA500U;
        items[n].label = "Spawn";
        n++;
    }
    return n;
}

static inline void
src_dungeon_regen(void *data) {
    src_dungeon_t *d = (src_dungeon_t *)data;
    d->seed += 1;
    src_dungeon_build(d);
}

/* --- SOURCE DESCRIPTOR --------------------------------------------------- */

static inline src_t
src_dungeon_source(src_dungeon_t *d) {
    assert(d);

    static const char *const mode_names[1] = { "Map" };
    static const char *const overlay_names[DUN_OVL_COUNT] = { "Spawn" };

    src_t s = {
        .name = (d->kind == DUN_SRC_BSP) ? "bsp" : "ca",
        .data = d,
        .w = DUN_W,
        .h = DUN_H,
        .bg = 0xFF101010U,
        .def_mode = 0,
        .def_overlays = DUN_OVL_SPAWN_BIT,
        .nmodes = 1,
        .mode_names = mode_names,
        .color = src_dungeon_color,
        .noverlays = DUN_OVL_COUNT,
        .overlay_names = overlay_names,
        .overlay = src_dungeon_overlay,
        .draw = src_dungeon_draw,
        .map_info = src_dungeon_map_info,
        .tile_info = src_dungeon_tile_info,
        .legend = src_dungeon_legend,
        .regen = src_dungeon_regen,
    };
    return s;
}

#endif /* SRC_DUNGEON_H */
