#ifndef RLK_TILEMAP_H
#define RLK_TILEMAP_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t    type;     /* game-defined tile value (opaque to rlk) */
    uint8_t    flags;    /* game-defined bits */
    uint8_t    biome;    /* game-defined zone id (continuity of regions) */
    uint8_t    variant;  /* game-defined variation within the type/biome */
} rlk_tile_t;

typedef struct {
    int         width;
    int         height;
    rlk_tile_t *tiles;  /* width*height cells, caller-provided */
} rlk_tilemap_t;

/* Init a tilemap over a caller-provided buffer. Asserts dimensions > 0 and
 * buffer large enough — the size check is the main value over raw POD init.
 */
static inline void
rlk_tilemap_init(rlk_tilemap_t *tm, int width, int height,
                 void *buf, size_t buf_sz) {
    assert(tm);
    assert(width > 0 && height > 0);
    assert(buf);
    assert(buf_sz >= (size_t)width * (size_t)height * sizeof(rlk_tile_t));

    tm->width  = width;
    tm->height = height;
    tm->tiles  = (rlk_tile_t *)buf;
}

/* Get a cell pointer at (x, y), or NULL if out of bounds. */
static inline rlk_tile_t *
rlk_tilemap_get(const rlk_tilemap_t *tm, int x, int y) {
    assert(tm);
    if (x < 0 || y < 0 || x >= tm->width || y >= tm->height) return NULL;
    return &tm->tiles[(size_t)y * (size_t)tm->width + (size_t)x];
}

#endif /* RLK_TILEMAP_H */
