#include "ca.h"
#include "fmath.h"
#include "hash.h"

#include <limits.h>

/* Internal cell states for the CA grid buffer (grid[] holds 0 or 1). */
enum { CA_WALL, CA_FLOOR };

/* Moore neighborhood (8-connected), excluding the cell itself. */
static const int NB8[8][2] = {
    {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1}
};

/* Forward neighbor offsets covering every 8-connected pair exactly once:
   right, down, down-right, down-left. */
static const int FWD4[4][2] = { {1,0}, {0,1}, {1,1}, {-1,1} };

static inline size_t
ca_idx(int x, int y, int w) {
    return (size_t)y * (size_t)w + (size_t)x;
}

/* --- SETUP --------------------------------------------------------------- */

void
rlk_ca_init(rlk_ca_t *ca) {
    assert(ca);

    /* Resolve zero defaults. */
    if (ca->fill_prob == 0.0f)
        ca->fill_prob = 0.45f;
    if (ca->iterations == 0)
        ca->iterations = 5;
    if (ca->birth_low == 0 && ca->birth_high == 0) {
        ca->birth_low  = 4;
        ca->birth_high = 5;
    }
    if (ca->survive_low == 0 && ca->survive_high == 0) {
        ca->survive_low  = 4;
        ca->survive_high = 8;
    }

    /* Safety checks (programmer errors): NULL buffers/callback, and config
       ranges after default resolution. */
    assert(ca->grid && ca->region_ids);
    assert(ca->write);
    assert(ca->fill_prob >= 0.0f && ca->fill_prob <= 1.0f);
    assert(ca->iterations > 0);
    assert(0 <= ca->birth_low && ca->birth_low <= ca->birth_high
           && ca->birth_high <= 8);
    assert(0 <= ca->survive_low && ca->survive_low <= ca->survive_high
           && ca->survive_high <= 8);
}

/* --- CA CORE ------------------------------------------------------------- */

static int
count_neighbors(const rlk_ca_t *ca, int x, int y) {
    assert(ca);
    int count = 0;
    for (int k = 0; k < 8; k++) {
        int nx = x + NB8[k][0];
        int ny = y + NB8[k][1];
        if (nx >= 0 && nx < ca->win_w && ny >= 0 && ny < ca->win_h) {
            if (ca->grid[ca_idx(nx, ny, ca->win_w)] == CA_FLOOR)
                count++;
        }
    }
    return count;
}

static void
seed_grid(rlk_ca_t *ca) {
    assert(ca);
    assert(ca->grid);

    for (int y = 0; y < ca->win_h; y++) {
        for (int x = 0; x < ca->win_w; x++) {
            float r = rlk_hash_normf(rlk_hash_at(ca->seed, x, y, 0xCA001ULL));
            ca->grid[ca_idx(x, y, ca->win_w)] =
                (r < ca->fill_prob) ? CA_FLOOR : CA_WALL;
        }
    }

    ca->region_count = 0;
}

static void
step(rlk_ca_t *ca) {
    assert(ca);
    assert(ca->grid);
    assert(ca->region_ids);

    int width = ca->win_w;
    int height = ca->win_h;
    int birth_low = ca->birth_low;
    int birth_high = ca->birth_high;
    int survive_low = ca->survive_low;
    int survive_high = ca->survive_high;

    /* region_ids is free during CA iterations: use its first W*H bytes as scratch. */
    uint8_t *scratch = (uint8_t *)ca->region_ids;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t current = ca->grid[ca_idx(x, y, width)];
            int neighbors = count_neighbors(ca, x, y);
            const int lo = (current == CA_FLOOR) ? survive_low : birth_low;
            const int hi = (current == CA_FLOOR) ? survive_high : birth_high;

            scratch[ca_idx(x, y, width)] =
                (neighbors >= lo && neighbors <= hi) ? CA_FLOOR : CA_WALL;
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = ca_idx(x, y, width);
            ca->grid[idx] = scratch[idx];
        }
    }
}

/* --- REGION LABELLING (union-find) --------------------------------------- */

static int
uf_find(int *parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];  /* path halving */
        x = parent[x];
    }
    return x;
}

static void
uf_union(int *parent, int a, int b) {
    int ra = uf_find(parent, a);
    int rb = uf_find(parent, b);
    if (ra != rb) {
        if (ra < rb) parent[rb] = ra;
        else         parent[ra] = rb;
    }
}

static int
find_regions(rlk_ca_t *ca) {
    assert(ca);
    assert(ca->grid);
    assert(ca->region_ids);

    int width = ca->win_w;
    int height = ca->win_h;
    size_t grid_size = (size_t)width * (size_t)height;

    /* Phase 1: init parent[i] = i for floor, -1 for wall. */
    for (size_t i = 0; i < grid_size; i++) {
        ca->region_ids[i] = (ca->grid[i] == CA_FLOOR) ? (int)i : -1;
    }

    /* Phase 2: union neighboring floor cells (8-connectivity); the forward
       offsets cover every pair exactly once. */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = ca_idx(x, y, width);
            if (ca->grid[idx] != CA_FLOOR) continue;

            for (int k = 0; k < 4; k++) {
                int nx = x + FWD4[k][0];
                int ny = y + FWD4[k][1];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                    continue;
                size_t nidx = ca_idx(nx, ny, width);
                if (ca->grid[nidx] == CA_FLOOR)
                    uf_union(ca->region_ids, (int)idx, (int)nidx);
            }
        }
    }

    /* Phase 3: compress paths — store root index in each floor cell. */
    for (size_t i = 0; i < grid_size; i++) {
        if (ca->grid[i] == CA_FLOOR)
            ca->region_ids[i] = uf_find(ca->region_ids, (int)i);
    }

    /* Phase 4: relabel roots to compact 1..N.
       Pass A: assign -label to each root's own slot.
       Pass B: propagate labels from roots to non-root floor cells.
       Pass C: convert root markers from -label to label. */
    int next_label = 1;
    for (size_t i = 0; i < grid_size; i++) {
        if (ca->grid[i] == CA_FLOOR && ca->region_ids[i] == (int)i)
            ca->region_ids[i] = -next_label++;
    }
    /* Pass B: non-root cells look up -label from their root's slot. */
    for (size_t i = 0; i < grid_size; i++) {
        if (ca->grid[i] != CA_FLOOR) {
            ca->region_ids[i] = 0;
        } else if (ca->region_ids[i] >= 0) {
            ca->region_ids[i] = -ca->region_ids[ca->region_ids[i]];
        }
    }
    /* Pass C: roots still hold -label; flip to positive. */
    for (size_t i = 0; i < grid_size; i++) {
        if (ca->region_ids[i] < 0)
            ca->region_ids[i] = -ca->region_ids[i];
    }

    ca->region_count = next_label - 1;
    return ca->region_count;
}

/* --- CONNECTIVITY -------------------------------------------------------- */

static void
find_region_center(const rlk_ca_t *ca, int region_id, int *center_x, int *center_y) {
    assert(ca);
    assert(ca->region_ids);
    assert(region_id >= 1 && region_id <= ca->region_count);

    int width = ca->win_w;
    int height = ca->win_h;
    int sum_x = 0, sum_y = 0, count = 0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = ca_idx(x, y, width);
            if (ca->region_ids[idx] == region_id) {
                sum_x += x;
                sum_y += y;
                count++;
            }
        }
    }

    if (count > 0) {
        *center_x = sum_x / count;
        *center_y = sum_y / count;
    } else {
        *center_x = 0;
        *center_y = 0;
    }
}

/* Carve corridors to connect all floor regions. Assumes the region labelling
   pass ran: ca->region_ids and ca->region_count must be valid. */
static void
connect_regions(rlk_ca_t *ca) {
    assert(ca);
    assert(ca->region_ids);

    int region_count = ca->region_count;
    if (region_count <= 1) {
        return;
    }

    int width = ca->win_w;
    int height = ca->win_h;

    for (int i = 2; i <= region_count; i++) {
        /* Compute center of region i */
        int ci_x, ci_y;
        find_region_center(ca, i, &ci_x, &ci_y);

        /* Find nearest region j < i (centers computed on the fly) */
        int nearest_j = 1;
        int min_center_dist = INT_MAX;
        for (int j = 1; j < i; j++) {
            int cj_x, cj_y;
            find_region_center(ca, j, &cj_x, &cj_y);
            int dist = rlk_dist_manhattan(ci_x, ci_y, cj_x, cj_y);
            if (dist < min_center_dist) {
                min_center_dist = dist;
                nearest_j = j;
            }
        }

        /* Find the closest pair of cells between region i and nearest_j */
        int x0 = -1, y0 = -1, x1 = -1, y1 = -1;
        int min_dist = INT_MAX;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                size_t idx = ca_idx(x, y, width);
                if (ca->region_ids[idx] != i) continue;

                for (int y2 = 0; y2 < height; y2++) {
                    for (int x2 = 0; x2 < width; x2++) {
                        size_t idx2 = ca_idx(x2, y2, width);
                        if (ca->region_ids[idx2] != nearest_j) continue;

                        int dist = rlk_dist_manhattan(x, y, x2, y2);
                        if (dist < min_dist) {
                            min_dist = dist;
                            x0 = x; y0 = y;
                            x1 = x2; y1 = y2;
                        }
                    }
                }
            }
        }

        if (x0 != -1) {
            int x = x0, y = y0;
            const int dx = (x1 > x0) ? 1 : ((x1 < x0) ? -1 : 0);
            const int dy = (y1 > y0) ? 1 : ((y1 < y0) ? -1 : 0);

            while (x != x1 || y != y1) {
                if (x != x1) x += dx;
                if (y != y1) y += dy;

                ca->grid[ca_idx(x, y, width)] = CA_FLOOR;
            }

            ca->grid[ca_idx(x1, y1, width)] = CA_FLOOR;
        }
    }
}

/* --- RENDER HELPERS ------------------------------------------------------ */

/* Largest floor region: id, cell count and bounding box (cell coords). */
static void
largest_region(const rlk_ca_t *ca, int *out_rid,
               int *min_x, int *min_y, int *max_x, int *max_y) {
    const int w = ca->win_w, h = ca->win_h;
    int best = 1, best_count = -1;
    int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;
    /* No allocation: one scan per region (region_count is small). */
    for (int rid = 1; rid <= ca->region_count; rid++) {
        int count = 0, x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (ca->region_ids[ca_idx(x, y, w)] == rid) {
                    count++;
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
            }
        }
        if (count > best_count) {
            best_count = count;
            best = rid;
            bx0 = x0; by0 = y0; bx1 = x1; by1 = y1;
        }
    }
    *out_rid = best;
    *min_x = bx0; *min_y = by0; *max_x = bx1; *max_y = by1;
}

/* --- PUBLIC API ---------------------------------------------------------- */

void
rlk_ca_fill(rlk_ca_t *ca, const rlk_lattice_t *win, rlk_tilemap_t *tm,
            const rlk_mask_t *mask, const rlk_poi_list_t *poi,
            rlk_poi_t *spawn) {
    assert(ca && win && tm);
    assert(win->w > 0 && win->h > 0 && win->cellsize > 0);
    assert(ca->grid && ca->region_ids && ca->write);
    if (mask)
        assert(rlk_lattice_same(&mask->lattice, win));

    const rlk_lattice_t *l = win;
    const int w = l->w, h = l->h, cs = l->cellsize;

    /* The tilemap must fully cover the window: caller contract. Each tile
       write is bounds-safe, but a partial window would silently truncate
       the fill, so check it once here. */
    assert(rlk_tilemap_contains_lattice(tm, l));

    ca->win_w = w;
    ca->win_h = h;

    /* Generate: seed, iterate, find regions, connect (optional), re-label. */
    seed_grid(ca);
    for (int i = 0; i < ca->iterations; i++) {
        step(ca);
    }
    find_regions(ca);
    if (ca->connect > 0) {
        connect_regions(ca);
        find_regions(ca);  /* re-label: corridors carved by connect are now floor */
    }
    /* The region id is preset into tile.biome (uint8_t). */
    assert(ca->region_count <= 255);

    /* Render: writer per cell, stamped over the cell's cs x cs tile block.
       Mask weight 0 and reserved POI bboxes are not written. */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t idx = ca_idx(x, y, w);
            if (mask && mask->weights[idx] == 0)
                continue;
            const int wx = l->origin_x + x * cs;
            const int wy = l->origin_y + y * cs;
            uint8_t cell = ca->grid[idx];
            int rid = (cell == CA_FLOOR) ? ca->region_ids[idx] : 0;
            /* Preset biome to region id, then the writer fills type/flags/variant. */
            rlk_tile_t tile = { .biome = (uint8_t)rid };
            ca->write(ca->seed, ca->write_data, wx, wy, &tile);
            for (int dy = 0; dy < cs; dy++) {
                for (int dx = 0; dx < cs; dx++) {
                    const int tx = wx + dx, ty = wy + dy;
                    if (!poi || !rlk_poi_at(poi, tx, ty))
                        rlk_tilemap_set(tm, tx, ty, tile);
                }
            }
        }
    }

    /* Entrance POI: the largest floor region. The anchor is its floor cell
       nearest to the bbox centre (the centre itself may be rock). */
    if (spawn && ca->region_count > 0) {
        int rid, x0, y0, x1, y1;
        largest_region(ca, &rid, &x0, &y0, &x1, &y1);
        const int ccx = (x0 + x1) / 2, ccy = (y0 + y1) / 2;
        int best_d = INT_MAX, ax = ccx, ay = ccy;
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                if (ca->region_ids[ca_idx(x, y, w)] != rid)
                    continue;
                int d = (x - ccx) * (x - ccx) + (y - ccy) * (y - ccy);
                if (d < best_d) {
                    best_d = d;
                    ax = x;
                    ay = y;
                }
            }
        }
        spawn->x = l->origin_x + ax * cs + cs / 2;
        spawn->y = l->origin_y + ay * cs + cs / 2;
        spawn->bbox = (rlk_rect_t){
            .x = l->origin_x + x0 * cs, .y = l->origin_y + y0 * cs,
            .w = (x1 - x0 + 1) * cs, .h = (y1 - y0 + 1) * cs,
        };
        spawn->seed = rlk_poi_seed(ca->seed, spawn->x, spawn->y, 0, 0x5EEDULL);
    }
}
