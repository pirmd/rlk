#ifndef SRC_NAV_H
#define SRC_NAV_H

/*
 * src_nav - viewer source for an in-game navigation scenario
 * (demo component, layer 5).
 *
 * A wall column with one door splits the map; the door opens
 * (incremental 1x1 mask update), A* finds a path
 * player -> target, then an entity spawns on the road and the path
 * detours. Here the path is an overlay (not drawn into the tilemap) and
 * player/target/entities are markers. R replays the scenario.
 *
 *   static src_nav_t n;
 *   src_nav_init(&n);
 *   src_t src = src_nav_source(&n);
 *
 * Header-only, POD, caller-owned, no allocation.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "pxl.h"
#include "source.h"
#include "rlk.h"

/* --- MAP CONSTANTS ------------------------------------------------------- */

#define NAV_W 64
#define NAV_H 40

#define NAV_PLAYER_X 4
#define NAV_PLAYER_Y 4
#define NAV_TARGET_X (NAV_W - 5)
#define NAV_TARGET_Y (NAV_H - 5)

#define NAV_DOOR_X (NAV_W / 2)
#define NAV_DOOR_Y 12

/* Tile types */
enum {
    N_GRASS = 0,
    N_WALL,
    N_WATER,
    N_ROCK,
    N_FLOOR,   /* open door */
    N_COUNT
};

/* Overlays */
enum {
    N_OVL_PATH = 0,   /* A* path player -> target */
    N_OVL_MARKS,      /* player / target / entity markers */
    N_OVL_COUNT
};

#define N_OVL_PATH_BIT  (1u << N_OVL_PATH)
#define N_OVL_MARKS_BIT (1u << N_OVL_MARKS)

static const uint32_t N_COLORS[N_COUNT] = {
    0xFF66BB6AU, /* grass: green */
    0xFF616161U, /* wall: gray */
    0xFF0277BD, /* water: blue */
    0xFF795548U, /* rock: brown */
    0xFFD7CCC8U, /* floor (open door): pale */
};

static const char *const N_NAMES[N_COUNT] = {
    "Grass", "Wall", "Water", "Rock", "Floor"
};

/* Walkability: grass/floor are walkable, water is not, wall/rock not. */
static const uint8_t N_FLAGS[N_COUNT] = {
    [N_GRASS] = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [N_WALL]  = 0,
    [N_WATER] = RLK_TILE_TRANSPARENT,
    [N_ROCK]  = 0,
    [N_FLOOR] = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
};

static const rlk_tile_props_t N_PROPS = { .flags = N_FLAGS, .ntypes = N_COUNT };

/* --- DYNAMIC COST (entities) --------------------------------------------- */

typedef struct {
    int x, y;
} nav_ent_t;

typedef struct {
    const rlk_mask_t *walk;
    const nav_ent_t *ents;
    int nents;
} nav_ctx_t;

/* Enter cost: blocked by the mask or by an entity, else 1. */
static inline int
nav_cost(void *user, int x, int y) {
    const nav_ctx_t *nav = (const nav_ctx_t *)user;
    if (!rlk_mask_allows(nav->walk, x, y))
        return RLK_PATH_BLOCKED;
    for (int i = 0; i < nav->nents; i++)
        if (nav->ents[i].x == x && nav->ents[i].y == y)
            return RLK_PATH_BLOCKED;
    return 1;
}

/* --- STATE --------------------------------------------------------------- */

typedef struct {
    rlk_tilemap_t tm;
    rlk_tile_t tiles[NAV_W * NAV_H];
    uint8_t walk[NAV_W * NAV_H];
    uint8_t on_path[NAV_W * NAV_H];
    int p_g[NAV_W * NAV_H];
    int p_parent[NAV_W * NAV_H];
    int p_heap[NAV_W * NAV_H];
    int p_heap_pos[NAV_W * NAV_H];
    uint8_t p_closed[NAV_W * NAV_H];
    int path[2 * NAV_W * NAV_H];
    int path_n;
    nav_ent_t ents[8];
    int nents;
    /* Pathfinding context (points into the fields above) */
    rlk_mask_t mask;
    nav_ctx_t nav;
    rlk_path_scratch_t scr;
    rlk_path_cost_t cost;
} src_nav_t;

/* Build the map and run the scenario (door closed -> open -> entity). */
static inline void
src_nav_build(src_nav_t *n) {
    assert(n);

    rlk_tilemap_init(&n->tm, NAV_W, NAV_H, n->tiles, sizeof(n->tiles));

    /* Grass everywhere, border walls, a full-height wall column at
       NAV_DOOR_X (the door cell starts closed = wall), a river with a ford
       gap, a few rocks. */
    for (int y = 0; y < NAV_H; y++)
        for (int x = 0; x < NAV_W; x++)
            rlk_tilemap_set(&n->tm, x, y, (rlk_tile_t){ .type = N_GRASS });

    for (int x = 0; x < NAV_W; x++) {
        rlk_tilemap_set(&n->tm, x, 0, (rlk_tile_t){ .type = N_WALL });
        rlk_tilemap_set(&n->tm, x, NAV_H - 1, (rlk_tile_t){ .type = N_WALL });
    }
    for (int y = 0; y < NAV_H; y++) {
        rlk_tilemap_set(&n->tm, 0, y, (rlk_tile_t){ .type = N_WALL });
        rlk_tilemap_set(&n->tm, NAV_W - 1, y, (rlk_tile_t){ .type = N_WALL });
        rlk_tilemap_set(&n->tm, NAV_DOOR_X, y, (rlk_tile_t){ .type = N_WALL });
    }
    for (int x = 6; x <= 26; x++) {
        rlk_tilemap_set(&n->tm, x, 18, (rlk_tile_t){ .type = N_WATER });
        rlk_tilemap_set(&n->tm, x, 19, (rlk_tile_t){ .type = N_WATER });
    }
    for (int x = 38; x <= 58; x++) {
        rlk_tilemap_set(&n->tm, x, 18, (rlk_tile_t){ .type = N_WATER });
        rlk_tilemap_set(&n->tm, x, 19, (rlk_tile_t){ .type = N_WATER });
    }
    rlk_tilemap_set(&n->tm, 20, 25, (rlk_tile_t){ .type = N_ROCK });
    rlk_tilemap_set(&n->tm, 21, 25, (rlk_tile_t){ .type = N_ROCK });
    rlk_tilemap_set(&n->tm, 20, 26, (rlk_tile_t){ .type = N_ROCK });
    rlk_tilemap_set(&n->tm, 45, 8, (rlk_tile_t){ .type = N_ROCK });
    rlk_tilemap_set(&n->tm, 46, 8, (rlk_tile_t){ .type = N_ROCK });

    /* Walkability mask from the tilemap (full recompute). */
    n->mask.lattice = rlk_tilemap_lattice(&n->tm);
    n->mask.v = n->walk;
    rlk_mask_from_tilemap(&n->mask, &n->tm, &N_PROPS, RLK_TILE_WALKABLE, NULL);

    /* Pathfinding setup: scratch + cost callback over the tilemap lattice. */
    n->scr = (rlk_path_scratch_t){ n->p_g, n->p_parent, n->p_heap, n->p_heap_pos, n->p_closed };
    n->nav.walk = &n->mask;
    n->nav.ents = n->ents;
    n->nav.nents = 0;
    n->cost = (rlk_path_cost_t){ .lattice = rlk_tilemap_lattice(&n->tm),
                                 .fn = nav_cost, .ctx = &n->nav };

    /* 1. Door closed: the wall column splits the map, no path exists
       (sanity check, result not displayed). */
    int ignored = 0;
    (void)rlk_path_find(&n->cost, &n->scr, NAV_PLAYER_X, NAV_PLAYER_Y,
                        NAV_TARGET_X, NAV_TARGET_Y,
                        n->path, NAV_W * NAV_H, &ignored);

    /* 2. Open the door: flip the tile, incremental 1x1 mask update. */
    rlk_tilemap_set(&n->tm, NAV_DOOR_X, NAV_DOOR_Y, (rlk_tile_t){ .type = N_FLOOR });
    rlk_rect_t dirty = { NAV_DOOR_X, NAV_DOOR_Y, 1, 1 };
    rlk_mask_from_tilemap(&n->mask, &n->tm, &N_PROPS, RLK_TILE_WALKABLE, &dirty);

    rlk_err_t err = rlk_path_find(&n->cost, &n->scr, NAV_PLAYER_X, NAV_PLAYER_Y,
                                  NAV_TARGET_X, NAV_TARGET_Y,
                                  n->path, NAV_W * NAV_H, &n->path_n);
    if (err != RLK_OK) {
        n->path_n = 0;
        return;
    }

    /* 3. An entity spawns on the road (left half, open ground): the path
       must detour around it. */
    int k = n->path_n / 2;   /* fallback */
    for (int i = n->path_n - 1; i >= 0; i--) {
        int px = n->path[2 * i], py = n->path[2 * i + 1];
        /* open ground, clear of the door corridor: a detour must exist */
        if (px < NAV_DOOR_X - 2 && py < 18) { k = i; break; }
    }
    n->ents[0] = (nav_ent_t){ 20, 10 };
    n->ents[1] = (nav_ent_t){ 50, 30 };
    n->ents[2] = (nav_ent_t){ n->path[2 * k], n->path[2 * k + 1] };
    n->nents = 3;
    n->nav.nents = 3;

    err = rlk_path_find(&n->cost, &n->scr, NAV_PLAYER_X, NAV_PLAYER_Y,
                        NAV_TARGET_X, NAV_TARGET_Y,
                        n->path, NAV_W * NAV_H, &n->path_n);
    if (err != RLK_OK) {
        n->path_n = 0;
        return;
    }

    /* Path overlay mask */
    for (int i = 0; i < NAV_W * NAV_H; i++)
        n->on_path[i] = 0;
    for (int i = 0; i < n->path_n; i++) {
        int px = n->path[2 * i], py = n->path[2 * i + 1];
        if (px >= 0 && px < NAV_W && py >= 0 && py < NAV_H)
            n->on_path[py * NAV_W + px] = 1;
    }
}

static inline void
src_nav_init(src_nav_t *n) {
    assert(n);
    src_nav_build(n);
}

/* --- SOURCE CALLBACKS ---------------------------------------------------- */

static inline uint32_t
src_nav_color(void *data, int mode, int wx, int wy) {
    (void)mode;
    const src_nav_t *n = (const src_nav_t *)data;
    const rlk_tile_t *t = rlk_tilemap_get(&n->tm, wx, wy);
    if (!t) return 0xFF101010U;
    return N_COLORS[t->type % N_COUNT];
}

static inline uint32_t
src_nav_overlay(void *data, int o, int wx, int wy, uint32_t base) {
    const src_nav_t *n = (const src_nav_t *)data;
    if (o == N_OVL_PATH && wx >= 0 && wx < NAV_W && wy >= 0 && wy < NAV_H
        && n->on_path[wy * NAV_W + wx])
        return 0xFFFFFF00U;   /* path: yellow */
    return base;
}

static inline void
src_nav_draw(pxl_canvas_t *cnv, void *data, int o, int mode, const src_view_t *v) {
    assert(cnv && data && v);
    (void)mode;
    const src_nav_t *n = (const src_nav_t *)data;
    if (o != N_OVL_MARKS)
        return;

    int half = v->tile_px / 4;
    /* player, target, entities: filled square + white outline */
    static const struct { int x, y; uint32_t c; } marks[] = {
        { NAV_PLAYER_X, NAV_PLAYER_Y, 0xFFFF0000U },  /* player: red */
        { NAV_TARGET_X, NAV_TARGET_Y, 0xFF00FF00U },  /* target: green */
    };
    for (int m = 0; m < 2; m++) {
        int sx = marks[m].x - v->wx;
        int sy = marks[m].y - v->wy;
        if (sx < 0 || sx >= v->vw || sy < 0 || sy >= v->vh) continue;
        int cx = v->dx + sx * v->tile_px + v->tile_px / 2;
        int cy = v->dy + sy * v->tile_px + v->tile_px / 2;
        pxl_canvas_set_color(cnv, marks[m].c);
        pxl_fill_rect(cnv, cx - half, cy - half, half * 2, half * 2);
        pxl_canvas_set_color(cnv, 0xFFFFFFFFU);
        pxl_draw_rect(cnv, cx - half, cy - half, half * 2, half * 2);
    }
    for (int i = 0; i < n->nents; i++) {
        int sx = n->ents[i].x - v->wx;
        int sy = n->ents[i].y - v->wy;
        if (sx < 0 || sx >= v->vw || sy < 0 || sy >= v->vh) continue;
        int cx = v->dx + sx * v->tile_px + v->tile_px / 2;
        int cy = v->dy + sy * v->tile_px + v->tile_px / 2;
        pxl_canvas_set_color(cnv, 0xFF9C27B0U);   /* entity: purple */
        pxl_fill_rect(cnv, cx - half, cy - half, half * 2, half * 2);
        pxl_canvas_set_color(cnv, 0xFFFFFFFFU);
        pxl_draw_rect(cnv, cx - half, cy - half, half * 2, half * 2);
    }
}

static inline void
src_nav_map_info(char *buf, size_t cap, const void *data) {
    const src_nav_t *n = (const src_nav_t *)data;
    snprintf(buf, cap,
        "     MAP\n\n"
        "Size   : %d x %d\n"
        "Player : (%d,%d)\n"
        "Target : (%d,%d)\n"
        "Door   : (%d,%d) open\n"
        "Path   : %d steps\n"
        "Ents   : %d (R: replay)",
        NAV_W, NAV_H,
        NAV_PLAYER_X, NAV_PLAYER_Y,
        NAV_TARGET_X, NAV_TARGET_Y,
        NAV_DOOR_X, NAV_DOOR_Y,
        n->path_n, n->nents);
}

static inline void
src_nav_tile_info(char *buf, size_t cap, const void *data, int wx, int wy) {
    const src_nav_t *n = (const src_nav_t *)data;
    const rlk_tile_t *t = rlk_tilemap_get(&n->tm, wx, wy);
    int on_path = (wx >= 0 && wx < NAV_W && wy >= 0 && wy < NAV_H)
                  ? n->on_path[wy * NAV_W + wx] : 0;
    int ent = -1;
    for (int i = 0; i < n->nents; i++)
        if (n->ents[i].x == wx && n->ents[i].y == wy) ent = i;
    snprintf(buf, cap,
        "     TILE\n\n"
        "Position: (%d,%d)\n"
        "Type    : %s (%d)\n"
        "Walkable: %s\n"
        "On path : %s\n"
        "Entity  : %s",
        wx, wy,
        t ? N_NAMES[t->type % N_COUNT] : "?", t ? t->type : -1,
        (t && (N_FLAGS[t->type % N_COUNT] & RLK_TILE_WALKABLE)) ? "yes" : "no",
        on_path ? "yes" : "no",
        ent >= 0 ? "yes" : "no");
}

static inline int
src_nav_legend(widget_item_t *items, int cap, const void *data,
               int mode, unsigned overlays) {
    (void)data; (void)mode;
    int n = 0;
    for (int i = 0; i < N_COUNT && n < cap; i++) {
        items[n].color = N_COLORS[i];
        items[n].label = N_NAMES[i];
        n++;
    }
    if ((overlays & N_OVL_PATH_BIT) && n < cap) {
        items[n].color = 0xFFFFFF00U;
        items[n].label = "Path";
        n++;
    }
    if ((overlays & N_OVL_MARKS_BIT) && n + 3 <= cap) {
        items[n++] = (widget_item_t){ 0xFFFF0000U, "Player" };
        items[n++] = (widget_item_t){ 0xFF00FF00U, "Target" };
        items[n++] = (widget_item_t){ 0xFF9C27B0U, "Entity" };
    }
    return n;
}

static inline void
src_nav_regen(void *data) {
    src_nav_build((src_nav_t *)data);   /* deterministic replay */
}

/* --- SOURCE DESCRIPTOR --------------------------------------------------- */

static inline src_t
src_nav_source(src_nav_t *n) {
    assert(n);

    static const char *const mode_names[1] = { "Map" };
    static const char *const overlay_names[N_OVL_COUNT] = { "Path", "Marks" };

    src_t s = {
        .name = "nav",
        .data = n,
        .w = NAV_W,
        .h = NAV_H,
        .bg = 0xFF101010U,
        .def_mode = 0,
        .def_overlays = N_OVL_PATH_BIT | N_OVL_MARKS_BIT,
        .nmodes = 1,
        .mode_names = mode_names,
        .color = src_nav_color,
        .noverlays = N_OVL_COUNT,
        .overlay_names = overlay_names,
        .overlay = src_nav_overlay,
        .draw = src_nav_draw,
        .map_info = src_nav_map_info,
        .tile_info = src_nav_tile_info,
        .legend = src_nav_legend,
        .regen = src_nav_regen,
    };
    return s;
}

#endif /* SRC_NAV_H */
