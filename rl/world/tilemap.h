#ifndef RL_TILEMAP_H
#define RL_TILEMAP_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../err.h"

/*
 * rlk Tilemap — a 2D grid of tiles with an occupation mask.
 *
 * Allocation follows Pattern A (docs/ALLOCATION.md): the caller provides the
 * backing buffer; rlk never allocates. `tiles` points into that buffer.
 *
 * The `occ_mask` is a packed 1-bit-per-tile obstacle mask stored inline in the
 * same buffer (immediately after the tiles array). It is the deterministic base
 * for solidity; mutations from harvesting are applied as a persisted delta at
 * load time (see docs/ARCHITECTURE.md, `occ_mask = generated XOR harvested`).
 * Here we only expose the deterministic base + read/write helpers.
 */

/* Tile types. Phase 0: a minimal palette; richer biome tiles come later. */
typedef enum {
    RL_TILE_VOID    = 0,   /* ungenerated / out of bounds */
    RL_TILE_FLOOR   = 1,
    RL_TILE_GRASS   = 2,
    RL_TILE_DIRT    = 3,
    RL_TILE_STONE   = 4,
    RL_TILE_WATER   = 5,
    RL_TILE_SAND    = 6,
    RL_TILE_COUNT
} rlk_tile_t;

/* Compact per-tile data. POD, fixed size for stable layout. */
typedef struct {
    uint8_t  type;     /* rlk_tile_t */
    uint8_t  biome;    /* biome id (placeholder for later phases) */
} rlk_tilecell_t;

typedef struct {
    int32_t          width, height;
    float            tile_size;   /* world units per tile */
    rlk_tilecell_t  *tiles;       /* width*height cells, caller-provided */
    uint8_t         *occ_mask;    /* packed obstacle mask, (w*h+7)/8 bytes */
} rlk_tilemap_t;

/* Configuration for size queries. POD, no allocations. */
typedef struct {
    int32_t width, height;
} rlk_tilemap_cfg_t;

/* Exact byte size of the backing buffer needed for width*height cells + mask. */
static inline size_t
rlk_tilemap_required_size(int32_t width, int32_t height) {
    assert(width > 0 && height > 0);
    size_t cells = (size_t)width * (size_t)height;
    size_t mask_bytes = (cells + 7u) / 8u;
    return cells * sizeof(rlk_tilecell_t) + mask_bytes;
}

/* Compile-time buffer size for a fixed-size stack/static tilemap buffer.
   Suitable for `unsigned char buf[RLK_TM_BUF_SIZE(W, H)];`. */
#define RLK_TM_BUF_SIZE(width, height) \
    ((size_t)(width) * (size_t)(height) * sizeof(rlk_tilecell_t) \
     + (((size_t)(width) * (size_t)(height) + 7u) / 8u))

/*
 * Initialize a tilemap over a caller-provided buffer. The buffer must be at
 * least rlk_tilemap_required_size(width, height) bytes. Fails RLK_E_BUFSIZE on
 * too-small buffer, RLK_E_CONFIG on non-positive dimensions. Zero-inits the
 * buffer (tiles cleared to RL_TILE_VOID, mask cleared to 0).
 */
static inline rlk_err_t
rlk_tilemap_init(rlk_tilemap_t *tm, int32_t width, int32_t height, float tile_size,
                 void *buffer, size_t buffer_size) {
    assert(tm);
    if (width <= 0 || height <= 0 || tile_size <= 0.0f) {
        return RLK_E_CONFIG;
    }
    size_t need = rlk_tilemap_required_size(width, height);
    if (buffer == NULL || buffer_size < need) {
        return RLK_E_BUFSIZE;
    }
    memset(buffer, 0, need);
    tm->width     = width;
    tm->height    = height;
    tm->tile_size = tile_size;
    tm->tiles     = (rlk_tilecell_t *)buffer;
    tm->occ_mask  = (uint8_t *)buffer + (size_t)width * (size_t)height * sizeof(rlk_tilecell_t);
    return RLK_OK;
}

static inline int
rlk_tilemap_in_bounds(const rlk_tilemap_t *tm, int32_t x, int32_t y) {
    assert(tm);
    return x >= 0 && y >= 0 && x < tm->width && y < tm->height;
}

/* Index of cell (x, y). Caller ensures in_bounds. */
static inline int32_t
rlk_tilemap_index(const rlk_tilemap_t *tm, int32_t x, int32_t y) {
    assert(tm);
    assert(rlk_tilemap_in_bounds(tm, x, y));
    return y * tm->width + x;
}

/* Get cell pointer, or NULL if out of bounds. */
static inline rlk_tilecell_t *
rlk_tilemap_get(const rlk_tilemap_t *tm, int32_t x, int32_t y) {
    assert(tm);
    if (!rlk_tilemap_in_bounds(tm, x, y)) return NULL;
    return &tm->tiles[rlk_tilemap_index(tm, x, y)];
}

/*
 * Set the obstacle bit for cell (x, y). Returns RLK_E_RANGE if out of bounds.
 * `solid` toggles occupancy on (1) or off (0).
 */
static inline rlk_err_t
rlk_tilemap_set_occ(rlk_tilemap_t *tm, int32_t x, int32_t y, int solid) {
    assert(tm);
    if (!rlk_tilemap_in_bounds(tm, x, y)) return RLK_E_RANGE;
    int32_t i = rlk_tilemap_index(tm, x, y);
    uint32_t ui = (uint32_t)i;
    uint8_t bit = (uint8_t)(1u << (ui & 7u));
    if (solid) tm->occ_mask[ui >> 3] |= bit;
    else       tm->occ_mask[ui >> 3] &= (uint8_t)~bit;
    return RLK_OK;
}

/* Query the obstacle bit for cell (x, y). Out of bounds => solid (1). */
static inline int
rlk_tilemap_is_solid(const rlk_tilemap_t *tm, int32_t x, int32_t y) {
    assert(tm);
    if (!rlk_tilemap_in_bounds(tm, x, y)) return 1;
    uint32_t ui = (uint32_t)rlk_tilemap_index(tm, x, y);
    return (tm->occ_mask[ui >> 3] >> (ui & 7u)) & 1u;
}

/*
 * Circle solidity test against the tile grid: returns 1 if any solid tile
 * overlaps the circle at world position (cx, cy) of radius r. Tiles are
 * axis-aligned squares of size tm->tile_size. Used later by flee/AI queries.
 */
static inline int
rlk_tilemap_is_solid_circle(const rlk_tilemap_t *tm, float cx, float cy, float r) {
    assert(tm && tm->tile_size > 0.0f);
    int32_t x0 = (int32_t)((cx - r) / tm->tile_size);
    int32_t y0 = (int32_t)((cy - r) / tm->tile_size);
    int32_t x1 = (int32_t)((cx + r) / tm->tile_size);
    int32_t y1 = (int32_t)((cy + r) / tm->tile_size);
    for (int32_t ty = y0; ty <= y1; ty++) {
        for (int32_t tx = x0; tx <= x1; tx++) {
            if (!rlk_tilemap_in_bounds(tm, tx, ty)) return 1;
            if (rlk_tilemap_is_solid(tm, tx, ty)) {
                /* Nearest point on the tile's axis-aligned box to the center. */
                float ts = tm->tile_size;
                float bx = (float)tx * ts;   /* tile min x */
                float by = (float)ty * ts;     /* tile min y */
                float nx = cx;
                if (nx < bx)      nx = bx;
                else if (nx > bx + ts) nx = bx + ts;
                float ny = cy;
                if (ny < by)      ny = by;
                else if (ny > by + ts) ny = by + ts;
                float dx = cx - nx;
                float dy = cy - ny;
                if (dx * dx + dy * dy <= r * r) return 1;
            }
        }
    }
    return 0;
}

#endif /* RL_TILEMAP_H */
