#ifndef RLK_GEOM_H
#define RLK_GEOM_H

/* World geometry: rects and lattices over the tile world. */

#include <assert.h>

/* --- Region ------------------------------------------------------------- */

typedef struct { int x, y, w, h; } rlk_rect_t;

/* 1 if (x,y) is inside r, else 0 */
static inline int
rlk_rect_contains(const rlk_rect_t *r, int x, int y) {
    assert(r);
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* Intersection of a and b. Returns 1 and sets *out if non-empty, else 0. */
static inline int
rlk_rect_intersect(rlk_rect_t *out, const rlk_rect_t *a, const rlk_rect_t *b) {
    assert(out && a && b);
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
    int y1 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
    if (x0 >= x1 || y0 >= y1) return 0;
    out->x = x0; out->y = y0; out->w = x1 - x0; out->h = y1 - y0;
    return 1;
}

/* --- Lattice ------------------------------------------------------------ */

/* A regular grid of cells over the world: the geotransform of
 * a raster (wxh cells of cellsize tiles at origin origin_x,origin_y).
 */
typedef struct {
    int w, h;                /* in cells, >0 */
    int cellsize;            /* tiles per cell side, >= 1 */
    int origin_x, origin_y;  /* world tile of the top-left corner */
} rlk_lattice_t;

/* Linear index (j * w + i) of the cell containing tile (x, y), or -1 if
   outside. Bounds-check before division: C integer division truncates
   toward zero, which would misplace negative coordinates. */
static inline int
rlk_lattice_index(const rlk_lattice_t *l, int x, int y) {
    assert(l && l->w > 0 && l->h > 0 && l->cellsize > 0);
    int dx = x - l->origin_x, dy = y - l->origin_y;
    if (dx < 0 || dy < 0) return -1;
    int i = dx / l->cellsize, j = dy / l->cellsize;
    if (i >= l->w || j >= l->h) return -1;
    return j * l->w + i;
}

/* 1 if both lattices have identical geometry. */
static inline int
rlk_lattice_same(const rlk_lattice_t *a, const rlk_lattice_t *b) {
    assert(a && b);
    return a->w == b->w && a->h == b->h
        && a->cellsize == b->cellsize
        && a->origin_x == b->origin_x && a->origin_y == b->origin_y;
}

#endif /* RLK_GEOM_H */
