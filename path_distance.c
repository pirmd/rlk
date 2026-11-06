#include "path_distance.h"

#include <assert.h>
#include <stdint.h>

#define RLK_PATH_DISTANCE_MAX_CELLS (1 << 20) /* window size limit (asserted): keeps g below RLK_PATH_DISTANCE_INF */

/* 4 cardinals (E, S, W, N) then 4 diagonals (SE, SW, NE, NW). */
static const int8_t DX[8] = { 1, 0, -1, 0, 1, -1, 1, -1 };
static const int8_t DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };

/* Enter cost of cell (i, j) in cell coordinates (caller: inside window).
   Callback values are clamped to 0..255 (0 = blocked). */
static int
cost_at(const rlk_path_cost_t *c, int i, int j) {
    if (c->table)
        return c->table[(size_t)j * (size_t)c->lattice.w + (size_t)i];
    int v = c->fn(c->ctx, c->lattice.origin_x + i, c->lattice.origin_y + j);
    if (v < 0)
        return RLK_PATH_BLOCKED;
    if (v > 255)
        v = 255;
    return v;
}

/* --- Binary heap ordered by g (plain Dijkstra, no heuristic) ------------ */

/* Smaller g first; ties: smaller cell index (deterministic). */
static int
heap_less(const rlk_path_distance_scratch_t *s, int a, int b) {
    if (s->g[a] != s->g[b])
        return s->g[a] < s->g[b];
    return a < b;
}

static void
heap_swap(rlk_path_distance_scratch_t *s, int a, int b) {
    int t = s->heap[a];
    s->heap[a] = s->heap[b];
    s->heap[b] = t;
    s->heap_pos[s->heap[a]] = a;
    s->heap_pos[s->heap[b]] = b;
}

static void
heap_sift_up(rlk_path_distance_scratch_t *s, int k) {
    while (k > 0) {
        int p = (k - 1) / 2;
        if (!heap_less(s, s->heap[k], s->heap[p]))
            break;
        heap_swap(s, k, p);
        k = p;
    }
}

static void
heap_sift_down(rlk_path_distance_scratch_t *s, int k, int n) {
    for (;;) {
        int left = 2 * k + 1, right = left + 1, best = k;
        if (left < n && heap_less(s, s->heap[left], s->heap[best]))
            best = left;
        if (right < n && heap_less(s, s->heap[right], s->heap[best]))
            best = right;
        if (best == k)
            break;
        heap_swap(s, k, best);
        k = best;
    }
}

static void
heap_push(rlk_path_distance_scratch_t *s, int *n, int cell) {
    s->heap[*n] = cell;
    s->heap_pos[cell] = *n;
    (*n)++;
    heap_sift_up(s, *n - 1);
}

static int
heap_pop(rlk_path_distance_scratch_t *s, int *n) {
    int top = s->heap[0];
    s->heap_pos[top] = -1;
    (*n)--;
    if (*n > 0) {
        s->heap[0] = s->heap[*n];
        s->heap_pos[s->heap[0]] = 0;
        heap_sift_down(s, 0, *n);
    }
    return top;
}

/* --- Public API ---------------------------------------------------------- */

rlk_err_t
rlk_path_distance_build(const rlk_path_cost_t *cost, rlk_path_distance_scratch_t *scr,
                        const int *sources, int nsrc) {
    assert(cost && scr);
    assert(sources || nsrc == 0);
    assert(cost->lattice.w > 0 && cost->lattice.h > 0);
    assert(cost->lattice.cellsize == 1);
    assert(cost->table || cost->fn);
    assert(scr->g && scr->parent && scr->heap && scr->heap_pos);
    assert(nsrc >= 0);

    const rlk_lattice_t *l = &cost->lattice;
    const int w = l->w, h = l->h;
    const size_t ncells = (size_t)w * (size_t)h;
    assert(ncells <= (size_t)RLK_PATH_DISTANCE_MAX_CELLS);

    /* Reset scratch. */
    for (size_t k = 0; k < ncells; k++) {
        scr->g[k] = RLK_PATH_DISTANCE_INF;
        scr->parent[k] = -1;
        scr->heap_pos[k] = -1;
    }

    /* Seed all sources with g = 0. */
    int heap_n = 0;
    for (int s = 0; s < nsrc; s++) {
        const int si = rlk_lattice_index(l, sources[2 * s], sources[2 * s + 1]);
        if (si < 0)
            return RLK_E_INVALID;
        if (cost_at(cost, si % w, si / w) == RLK_PATH_BLOCKED)
            continue;  /* blocked source: skipped */
        if (scr->g[si] == RLK_PATH_DISTANCE_INF) {
            scr->g[si] = 0;
            scr->parent[si] = -1;
            heap_push(scr, &heap_n, si);
        }
    }

    /* Flood: weights are positive, so a popped cell is settled (final g). */
    while (heap_n > 0) {
        const int cur = heap_pop(scr, &heap_n);
        const int ci = cur % w, cj = cur / w;
        for (int d = 0; d < 8; d++) {
            const int ni = ci + DX[d], nj = cj + DY[d];
            if (ni < 0 || nj < 0 || ni >= w || nj >= h)
                continue;
            const int nxt = nj * w + ni;
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
                step = 3 * c;
            } else {
                step = 2 * c;
            }
            const int ng = scr->g[cur] + step;
            if (ng < scr->g[nxt]) {
                scr->g[nxt] = ng;
                scr->parent[nxt] = cur;
                if (scr->heap_pos[nxt] >= 0)
                    heap_sift_up(scr, scr->heap_pos[nxt]);
                else
                    heap_push(scr, &heap_n, nxt);
            }
        }
    }

    return RLK_OK;
}

rlk_err_t
rlk_path_distance_find(const rlk_path_cost_t *cost, const rlk_path_distance_scratch_t *scr,
                       int x, int y, int *out, int cap, int *out_len) {
    assert(cost && scr && out && out_len);
    assert(scr->g && scr->parent);
    assert(cost->lattice.w > 0 && cost->lattice.h > 0);
    assert(cap >= 1);

    const rlk_lattice_t *l = &cost->lattice;
    const int si = rlk_lattice_index(l, x, y);
    if (si < 0)
        return RLK_E_INVALID;
    if (scr->g[si] >= RLK_PATH_DISTANCE_INF)
        return RLK_E_NOTFOUND;

    /* g strictly decreases along parent[], so the chain cannot cycle. */
    int len = 0;
    for (int c = si; c >= 0; c = scr->parent[c])
        len++;
    if (len > cap)
        return RLK_E_INVALID;
    for (int c = si, k = 0; c >= 0; c = scr->parent[c], k++) {
        out[2 * k]     = l->origin_x + c % l->w;
        out[2 * k + 1] = l->origin_y + c / l->w;
    }
    *out_len = len;
    return RLK_OK;
}
