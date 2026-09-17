#include "grid.h"

#include <stdlib.h>  /* for calloc, free */
#include <string.h>  /* for memset */

rlk_err_t
rlk_grid_init(rlk_grid_t *g, int width, int height) {
    if (!g || width <= 0 || height <= 0)
        return RLK_E_INVALID_PARAM;

    size_t count = (size_t)width * (size_t)height;
    rlk_tile_t *cells = calloc(count, sizeof(rlk_tile_t));
    if (!cells)
        return RLK_E_OUT_OF_MEM;

    g->width = width;
    g->height = height;
    g->cells = cells;
    return RLK_SUCCESS;
}

void
rlk_grid_deinit(rlk_grid_t *g) {
    if (!g)
        return;
    free(g->cells);
    g->cells = NULL;
    g->width = 0;
    g->height = 0;
}

void
rlk_grid_fill(rlk_grid_t *g, rlk_tile_t tile) {
    assert(g && g->cells);
    size_t count = (size_t)g->width * (size_t)g->height;
    if (tile == RLK_TILE_VOID) {
        memset(g->cells, 0, count * sizeof(rlk_tile_t));
        return;
    }
    for (size_t i = 0; i < count; ++i)
        g->cells[i] = tile;
}

void
rlk_grid_fill_rect(rlk_grid_t *g, int x, int y, int w, int h,
                   rlk_tile_t tile) {
    assert(g);

    if (w <= 0 || h <= 0)
        return;

    /* Clip to [0, width) x [0, height). */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > g->width)  x1 = g->width;
    int y1 = y + h; if (y1 > g->height) y1 = g->height;
    if (x0 >= x1 || y0 >= y1)
        return;

    for (int ry = y0; ry < y1; ++ry)
        for (int rx = x0; rx < x1; ++rx)
            g->cells[rlk_grid_index(g, rx, ry)] = tile;
}
