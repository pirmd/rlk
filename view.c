#include "view.h"

rlk_err_t
rlk_view_init(rlk_view_t *v, const rlk_grid_t *grid,
              int ox, int oy, int vw, int vh) {
    if (!v || !grid || vw <= 0 || vh <= 0)
        return RLK_E_INVALID_PARAM;

    /* Shrink an oversized view to the grid. */
    if (vw > grid->width)  vw = grid->width;
    if (vh > grid->height) vh = grid->height;

    /* Clamp origin so the [ox, ox+vw) window stays within the grid. */
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox + vw > grid->width)  ox = grid->width  - vw;
    if (oy + vh > grid->height) oy = grid->height - vh;

    v->grid = grid;
    v->ox = ox;
    v->oy = oy;
    v->vw = vw;
    v->vh = vh;
    return RLK_SUCCESS;
}

void
rlk_view_center(rlk_view_t *v, int cx, int cy) {
    assert(v && v->grid);

    int ox = cx - v->vw / 2;
    int oy = cy - v->vh / 2;

    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox + v->vw > v->grid->width)  ox = v->grid->width  - v->vw;
    if (oy + v->vh > v->grid->height) oy = v->grid->height - v->vh;

    v->ox = ox;
    v->oy = oy;
}

void
rlk_view_move(rlk_view_t *v, int dx, int dy) {
    assert(v && v->grid);

    int ox = v->ox + dx;
    int oy = v->oy + dy;

    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox + v->vw > v->grid->width)  ox = v->grid->width  - v->vw;
    if (oy + v->vh > v->grid->height) oy = v->grid->height - v->vh;

    v->ox = ox;
    v->oy = oy;
}
