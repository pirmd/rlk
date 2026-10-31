#include "path.h"

#include <limits.h>

#include "fmath.h"

#define RLK_PATH_MAX_CELLS (1 << 20) /* window size limit (scratch is ~17 B/cell) */

/* Unreachable g-score sentinel. Real costs stay below it: a path visits
   each cell at most once and a step costs at most (3*255+1)/2 = 383, so
   g < RLK_PATH_MAX_CELLS * 383 < INT_MAX/4. */
#define PATH_INF (INT_MAX / 4)

/* 4 cardinals (E, S, W, N) then 4 diagonals (SE, SW, NE, NW). */
static const int8_t DX[8] = { 1, 0, -1, 0, 1, -1, 1, -1 };
static const int8_t DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };

/* Enter cost of cell (i, j) in cell coordinates (caller: inside window). */
static int
cost_at(const rlk_path_cost_t *c, int i, int j) {
    if (c->table)
        return c->table[(size_t)j * (size_t)c->lattice.w + (size_t)i];
    int v = c->fn(c->ctx, c->lattice.origin_x + i, c->lattice.origin_y + j);
    assert(v >= 0 && v <= 255);
    return v;
}

/* --- Binary heap ordered by f = g + manhattan(cell, goal) ---------------- */

typedef struct {
    rlk_path_scratch_t *scr;
    const rlk_lattice_t *l;
    int gx, gy;
} path_heap_t;

/* Manhattan distance is admissible: every step costs at least 1. */
static int
path_f(const path_heap_t *hp, int cell) {
    const rlk_lattice_t *l = hp->l;
    int x = l->origin_x + cell % l->w;
    int y = l->origin_y + cell / l->w;
    return hp->scr->g[cell] + rlk_dist_manhattan(x, y, hp->gx, hp->gy);
}

/* Smaller f first; ties broken by smaller cell index (deterministic). */
static int
path_less(const path_heap_t *hp, int a, int b) {
    int fa = path_f(hp, a), fb = path_f(hp, b);
    return fa < fb || (fa == fb && a < b);
}

static void
path_swap(path_heap_t *hp, int a, int b) {
    rlk_path_scratch_t *s = hp->scr;
    int t = s->heap[a];
    s->heap[a] = s->heap[b];
    s->heap[b] = t;
    s->heap_pos[s->heap[a]] = a;
    s->heap_pos[s->heap[b]] = b;
}

static void
path_sift_up(path_heap_t *hp, int k) {
    while (k > 0) {
        int p = (k - 1) / 2;
        if (!path_less(hp, hp->scr->heap[k], hp->scr->heap[p]))
            break;
        path_swap(hp, k, p);
        k = p;
    }
}

static void
path_sift_down(path_heap_t *hp, int k, int n) {
    for (;;) {
        int left = 2 * k + 1, right = left + 1, best = k;
        if (left < n && path_less(hp, hp->scr->heap[left], hp->scr->heap[best]))
            best = left;
        if (right < n && path_less(hp, hp->scr->heap[right], hp->scr->heap[best]))
            best = right;
        if (best == k)
            break;
        path_swap(hp, k, best);
        k = best;
    }
}

static void
path_push(path_heap_t *hp, int *n, int cell) {
    rlk_path_scratch_t *s = hp->scr;
    s->heap[*n] = cell;
    s->heap_pos[cell] = *n;
    (*n)++;
    path_sift_up(hp, *n - 1);
}

static int
path_pop(path_heap_t *hp, int *n) {
    rlk_path_scratch_t *s = hp->scr;
    int top = s->heap[0];
    s->heap_pos[top] = -1;
    (*n)--;
    if (*n > 0) {
        s->heap[0] = s->heap[*n];
        s->heap_pos[s->heap[0]] = 0;
        path_sift_down(hp, 0, *n);
    }
    return top;
}

/* --- Public API ---------------------------------------------------------- */

rlk_err_t
rlk_path_find(const rlk_path_cost_t *cost, rlk_path_scratch_t *scr,
              int sx, int sy, int gx, int gy,
              int *out, int cap, int *out_len) {
    assert(cost && scr && out && out_len);
    assert(cost->lattice.w > 0 && cost->lattice.h > 0);
    assert(cost->lattice.cellsize == 1);
    assert(cost->table || cost->fn);
    assert(scr->g && scr->parent && scr->heap && scr->heap_pos && scr->closed);
    assert(cap >= 1);

    const rlk_lattice_t *l = &cost->lattice;
    const int w = l->w, h = l->h;
    const size_t ncells = (size_t)w * (size_t)h;

    const int si = rlk_lattice_index(l, sx, sy);
    const int gi = rlk_lattice_index(l, gx, gy);
    if (si < 0 || gi < 0)
        return RLK_E_INVALID;
    if (ncells > (size_t)RLK_PATH_MAX_CELLS)
        return RLK_E_INVALID;

    /* Reset scratch. */
    for (size_t k = 0; k < ncells; k++) {
        scr->g[k] = PATH_INF;
        scr->parent[k] = -1;
        scr->heap_pos[k] = -1;
        scr->closed[k] = 0;
    }

    path_heap_t hp = { .scr = scr, .l = l, .gx = gx, .gy = gy };
    int heap_n = 0;
    scr->g[si] = 0;
    path_push(&hp, &heap_n, si);

    while (heap_n > 0) {
        const int cur = path_pop(&hp, &heap_n);
        scr->closed[cur] = 1;
        if (cur == gi)
            break;  /* goal settled: g[gi] is optimal */

        const int ci = cur % w, cj = cur / w;
        for (int d = 0; d < 8; d++) {
            const int ni = ci + DX[d], nj = cj + DY[d];
            if (ni < 0 || nj < 0 || ni >= w || nj >= h)
                continue;
            const int c = cost_at(cost, ni, nj);
            if (c == RLK_PATH_BLOCKED)
                continue;
            int step;
            if (DX[d] != 0 && DY[d] != 0) {
                /* Diagonal: no corner cutting through blocked cardinals. */
                if (cost_at(cost, ci + DX[d], cj) == RLK_PATH_BLOCKED)
                    continue;
                if (cost_at(cost, ci, cj + DY[d]) == RLK_PATH_BLOCKED)
                    continue;
                step = (3 * c + 1) / 2;  /* ceil(3/2 * c) */
            } else {
                step = c;
            }
            const int nxt = nj * w + ni;
            if (scr->closed[nxt])
                continue;
            const int ng = scr->g[cur] + step;
            if (ng < scr->g[nxt]) {
                scr->g[nxt] = ng;
                scr->parent[nxt] = cur;
                if (scr->heap_pos[nxt] >= 0)
                    path_sift_up(&hp, scr->heap_pos[nxt]);
                else
                    path_push(&hp, &heap_n, nxt);
            }
        }
    }

    if (scr->g[gi] >= PATH_INF)
        return RLK_E_NOTFOUND;

    /* Reconstruct start -> goal. */
    int len = 0;
    for (int c = gi; c >= 0; c = scr->parent[c])
        len++;
    if (len > cap)
        return RLK_E_INVALID;
    for (int c = gi, k = len - 1; c >= 0; c = scr->parent[c], k--) {
        out[2 * k]     = l->origin_x + c % w;
        out[2 * k + 1] = l->origin_y + c / w;
    }
    *out_len = len;
    return RLK_OK;
}

void
rlk_path_draw(rlk_tilemap_t *tm, const int *path, int n,
              int width, rlk_tile_t tile) {
    assert(tm && path);
    assert(n >= 0);
    for (int k = 1; k < n; k++)
        rlk_tilemap_set_line(tm, path[2 * (k - 1)], path[2 * (k - 1) + 1],
                         path[2 * k], path[2 * k + 1], width, tile);
}
