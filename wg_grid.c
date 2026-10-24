#include "wg_grid.h"

#include <assert.h>

void
rlk_wg_grid_init(rlk_wg_grid_t *g, int w, int h, int cell, int ox, int oy,
                 float *buf) {
    assert(g && buf);
    assert(w >= 1 && h >= 1 && cell >= 1);

    g->w = w;
    g->h = h;
    g->cell = cell;
    g->ox = ox;
    g->oy = oy;
    g->v = buf;

    for (int k = 0; k < w * h; k++)
        buf[k] = 0.0f;
}

void
rlk_wg_grid_fill(rlk_wg_grid_t *g, rlk_wg_grid_fn fn, const void *user) {
    assert(g && fn);

    for (int j = 0; j < g->h; j++) {
        for (int i = 0; i < g->w; i++) {
            float px, py;
            rlk_wg_grid_center(g, i, j, &px, &py);
            g->v[(size_t)j * (size_t)g->w + (size_t)i] = fn(px, py, user);
        }
    }
}
