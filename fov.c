#include "fov.h"

#include <string.h>

/* Octant multipliers: cell = origin + (dx*m[0] + dy*m[1], dx*m[2] + dy*m[3])
   with dy = -row and dx in [-row, 0]. */
static const int M[8][4] = {
    { 1, 0, 0, -1 }, { -1, 0, 0, -1 },
    { 0, -1, -1, 0 }, { 0, -1, 1, 0 },
    { 0, 1, 1, 0 },   { 0, 1, -1, 0 },
    { -1, 0, 0, 1 },  { 1, 0, 0, 1 },
};

typedef struct {
    const rlk_lattice_t *win;
    const uint8_t *opaque;  /* NULL = nothing blocks */
    uint8_t *visible;
    int ox, oy;             /* view origin, world coords */
    int rmax;               /* last row scanned */
    int64_t r2;             /* squared view radius (euclidean) */
} fov_ctx_t;

/* 1 if the cell blocks sight. Out-of-window cells block (void beyond). */
static int
blocks(const fov_ctx_t *c, int x, int y) {
    int idx = rlk_lattice_index(c->win, x, y);
    if (idx < 0)
        return 1;
    return c->opaque && c->opaque[(size_t)idx];
}

/* Mark the cell visible if inside the window and within the radius. */
static void
mark(fov_ctx_t *c, int x, int y) {
    int idx = rlk_lattice_index(c->win, x, y);
    if (idx < 0)
        return;
    int64_t dx = (int64_t)x - c->ox, dy = (int64_t)y - c->oy;
    if (dx * dx + dy * dy <= c->r2)
        c->visible[(size_t)idx] = 1;
}

/* One octant of the recursive shadowcasting scan. */
static void
cast(fov_ctx_t *c, int row, float start, float end, const int *m) {
    if (start < end)
        return;
    float new_start = 0.0f;
    for (int j = row; j <= c->rmax; j++) {
        int dx = -j - 1;
        const int dy = -j;
        int blocked = 0;
        while (dx <= 0) {
            dx++;
            const int x = c->ox + dx * m[0] + dy * m[1];
            const int y = c->oy + dx * m[2] + dy * m[3];
            const float l_slope = ((float)dx - 0.5f) / ((float)dy + 0.5f);
            const float r_slope = ((float)dx + 0.5f) / ((float)dy - 0.5f);
            if (start < r_slope)
                continue;
            if (end > l_slope)
                break;
            mark(c, x, y);
            if (blocked) {
                if (blocks(c, x, y)) {
                    new_start = r_slope;
                    continue;
                }
                blocked = 0;
                start = new_start;
            } else if (blocks(c, x, y) && j < c->rmax) {
                blocked = 1;
                cast(c, j + 1, start, l_slope, m);
                new_start = r_slope;
            }
        }
        if (blocked)
            break;  /* no light gets past a full row of walls */
    }
}

void
rlk_fov_compute(uint8_t *visible, const rlk_lattice_t *win,
                const uint8_t *opaque, int ox, int oy, int radius) {
    assert(win && visible);
    assert(win->w > 0 && win->h > 0 && win->cellsize == 1);
    assert(rlk_lattice_index(win, ox, oy) >= 0);

    const size_t ncells = (size_t)win->w * (size_t)win->h;
    memset(visible, 0, ncells);

    fov_ctx_t c;
    c.win = win;
    c.opaque = opaque;
    c.visible = visible;
    c.ox = ox;
    c.oy = oy;
    /* No distance limit: the window bounds the view (a cell's octant row is
       its Chebyshev distance, bounded by w + h). */
    c.rmax = radius > 0 ? radius : win->w + win->h;
    c.r2 = radius > 0 ? (int64_t)radius * (int64_t)radius : INT64_MAX;

    mark(&c, ox, oy);
    for (int o = 0; o < 8; o++)
        cast(&c, 1, 1.0f, 0.0f, M[o]);
}
