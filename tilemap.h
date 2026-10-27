#ifndef RLK_TILEMAP_H
#define RLK_TILEMAP_H

/* layer: 1, domain: base */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "geom.h"

typedef struct {
    uint8_t    type;     /* game-defined tile value (opaque to rlk) */
    uint8_t    flags;    /* game-defined bits */
    uint8_t    biome;    /* game-defined zone id (continuity of regions) */
    uint8_t    variant;  /* game-defined variation within the type/biome */
    uint8_t    depth;    /* depth (water, underground): 0 = surface */
} rlk_tile_t;

/* Tile property flags (positive: a mask built from them is "allowed").
   A type outside the table has no flag (neither walkable nor transparent). */
enum {
    RLK_TILE_WALKABLE    = 1 << 0,
    RLK_TILE_TRANSPARENT = 1 << 1
    /* following bits reserved for the application */
};

/* Per-type properties, provided by the game. */
typedef struct {
    const uint8_t *flags;   /* ntypes entries */
    int            ntypes;
} rlk_tile_props_t;

typedef struct {
    int         w, h;    /* dimensions in tiles */
    int         origin_x, origin_y;  /* world origin, in tiles */
    rlk_tile_t *tiles;   /* w*h tiles, caller-provided */
} rlk_tilemap_t;

/* Init a tilemap over a caller-provided buffer at world origin (0, 0).
   Asserts dimensions > 0 and buffer large enough — the size check is the
   main value over raw POD init. Does not zero the buffer. */
static inline void
rlk_tilemap_init(rlk_tilemap_t *tm, int w, int h,
                 void *buf, size_t buf_sz) {
    assert(tm);
    assert(w > 0 && h > 0);
    assert(buf);
    assert(buf_sz >= (size_t)w * (size_t)h * sizeof(rlk_tile_t));

    tm->w     = w;
    tm->h     = h;
    tm->origin_x    = 0;
    tm->origin_y    = 0;
    tm->tiles = (rlk_tile_t *)buf;
}

/* Get a tile pointer at world position (x, y), or NULL if outside the
   tilemap. World coords: bounds [origin_x, origin_x+w) x [origin_y, origin_y+h). For a tilemap at
   origin (0,0) this is identical to local coordinates. */
static inline rlk_tile_t *
rlk_tilemap_get(const rlk_tilemap_t *tm, int x, int y) {
    assert(tm);
    int lx = x - tm->origin_x, ly = y - tm->origin_y;
    if (lx < 0 || ly < 0 || lx >= tm->w || ly >= tm->h) return NULL;
    return &tm->tiles[(size_t)ly * (size_t)tm->w + (size_t)lx];
}

/* Write a tile at world position (x, y). No effect if outside the tilemap. */
static inline void
rlk_tilemap_set(rlk_tilemap_t *tm, int x, int y, rlk_tile_t tile) {
    assert(tm);
    rlk_tile_t *t = rlk_tilemap_get(tm, x, y);
    if (t) *t = tile;
}

/* Lattice of the tilemap: tile-aligned (cellsize = 1), origin = tilemap origin. */
static inline rlk_lattice_t
rlk_tilemap_lattice(const rlk_tilemap_t *tm) {
    assert(tm);
    rlk_lattice_t l = { .w = tm->w, .h = tm->h, .cellsize = 1,
                        .origin_x = tm->origin_x, .origin_y = tm->origin_y };
    return l;
}

/* Trace a segment from (x0,y0) to (x1,y1), square brush of `width` tiles
   centred on the line (width >= 1). Clipped to the tilemap. Deterministic. */
static inline void
rlk_tilemap_line(rlk_tilemap_t *tm, int x0, int y0,
                 int x1, int y1, int width, rlk_tile_t tile) {
    assert(tm);
    assert(width >= 1);
    int r = width / 2;
    int dx =  x1 - x0; if (dx < 0) dx = -dx;
    int dy =  y1 - y0; if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        for (int j = -r; j <= r; j++)
            for (int i = -r; i <= r; i++)
                rlk_tilemap_set(tm, x0 + i, y0 + j, tile);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

#endif /* RLK_TILEMAP_H */
