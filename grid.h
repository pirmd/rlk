#ifndef RLK_GRID_H
#define RLK_GRID_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>  /* for size_t */
#include <stdint.h>  /* for uint16_t */

#include "err.h"

/*
 * Grid: a 2D array of cells indexed by (x, y).
 *
 * Each cell holds a tile value (rlk_tile_t). A tile value identifies a kind
 * of terrain/occupant: 0 is conventionally the "empty/void" tile, positive
 * values map to game-defined tile kinds. RLK itself does not interpret tile
 * values; interpretation (graphics, walkability, ...) is left to the user.
 *
 * The grid owns its cell storage. Grids are intentionally simple and meant
 * to be rendered through a companion graphics library such as PXL (see the
 * demo).
 */
typedef uint16_t rlk_tile_t;

#define RLK_TILE_VOID ((rlk_tile_t)0)

typedef struct {
    int        width;   /* grid width in cells                    */
    int        height;  /* grid height in cells                   */
    rlk_tile_t *cells;  /* width * height cells, row-major (y,x)  */
} rlk_grid_t;

/* Size helpers ----------------------------------------------------------- */
static inline int
rlk_grid_size(const rlk_grid_t *g) {
    assert(g);
    return g->width * g->height;
}

/* True if (x, y) is a valid cell coordinate. */
static inline bool
rlk_grid_in(const rlk_grid_t *g, int x, int y) {
    assert(g);
    return x >= 0 && x < g->width && y >= 0 && y < g->height;
}

/* Internal: index of cell (x, y) without bounds checking. */
static inline int
rlk_grid_index(const rlk_grid_t *g, int x, int y) {
    assert(g);
    assert(rlk_grid_in(g, x, y));
    return x + y * g->width;
}

/* Lifecycle -------------------------------------------------------------- */

/* Initialize a grid of the given size, filled with RLK_TILE_VOID.
 * Returns RLK_E_INVALID_PARAM if width or height is non-positive, or
 * RLK_E_OUT_OF_MEM if allocation fails. Use rlk_grid_deinit() to free. */
rlk_err_t rlk_grid_init(rlk_grid_t *g, int width, int height);

/* Free grid storage. Safe on an already-deinitialized or zeroed grid. */
void rlk_grid_deinit(rlk_grid_t *g);

/* Cell access ------------------------------------------------------------ */

/* Get the tile at (x, y). Out-of-bounds reads return RLK_TILE_VOID. */
static inline rlk_tile_t
rlk_grid_get(const rlk_grid_t *g, int x, int y) {
    assert(g);
    if (!rlk_grid_in(g, x, y))
        return RLK_TILE_VOID;
    return g->cells[rlk_grid_index(g, x, y)];
}

/* Set the tile at (x, y). Out-of-bounds writes are ignored. */
static inline void
rlk_grid_set(rlk_grid_t *g, int x, int y, rlk_tile_t tile) {
    assert(g);
    if (!rlk_grid_in(g, x, y))
        return;
    g->cells[rlk_grid_index(g, x, y)] = tile;
}

/* Fill the whole grid with a single tile value. */
void rlk_grid_fill(rlk_grid_t *g, rlk_tile_t tile);

/* Fill a rectangular region [x, y, w, h) with tile. Region is clipped to the
 * grid bounds; out-of-bounds regions are a no-op. */
void rlk_grid_fill_rect(rlk_grid_t *g, int x, int y, int w, int h,
                        rlk_tile_t tile);

#endif /* RLK_GRID_H */
