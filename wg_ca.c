#include "wg_ca.h"
#include "fmath.h"
#include "hash.h"

#include <limits.h>

/* Internal cell states for the CA grid buffer (grid[] holds 0 or 1). */
enum { CA_WALL, CA_FLOOR };

static inline size_t
ca_idx(int x, int y, int w) {
    return (size_t)y * (size_t)w + (size_t)x;
}

/* --- SETUP --------------------------------------------------------------- */

/* Safety checks only: prevent crashes (buffer overflow, NULL deref).
   Logic errors (fill_prob range, birth/survive ranges) are the user's
   responsibility. */
static void
assert_ca(const rlk_wg_ca_t *ca) {
    assert(ca);
    assert(ca->width > 0 && ca->height > 0);
    assert(ca->grid && ca->region_ids);
    assert(ca->write);
}

void
rlk_wg_ca_init(rlk_wg_ca_t *ca) {
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

    /* Safety checks. */
    assert_ca(ca);
}

/* --- CA CORE ------------------------------------------------------------- */

static int
count_neighbors(const rlk_wg_ca_t *ca, int x, int y) {
    assert(ca);
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < ca->width && ny >= 0 && ny < ca->height) {
                if (ca->grid[ca_idx(nx, ny, ca->width)] == CA_FLOOR) {
                    count++;
                }
            }
        }
    }
    return count;
}

static void
seed_grid(rlk_wg_ca_t *ca) {
    assert(ca);
    assert(ca->grid);

    for (int y = 0; y < ca->height; y++) {
        for (int x = 0; x < ca->width; x++) {
            uint64_t h = rlk_hash_sub(rlk_hash_combine2(ca->seed, x, y), 0xCA001ULL);
            float r = (float)(h >> 40) * (1.0f / 16777216.0f);
            ca->grid[ca_idx(x, y, ca->width)] =
                (r < ca->fill_prob) ? CA_FLOOR : CA_WALL;
        }
    }

    ca->cur_iter = 0;
    ca->region_count = 0;
}

static void
step(rlk_wg_ca_t *ca) {
    assert(ca);
    assert(ca->grid);
    assert(ca->region_ids);

    int width = ca->width;
    int height = ca->height;
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
            uint8_t next;

            if (current == CA_FLOOR) {
                if (neighbors >= survive_low && neighbors <= survive_high) {
                    next = CA_FLOOR;
                } else {
                    next = CA_WALL;
                }
            } else {
                if (neighbors >= birth_low && neighbors <= birth_high) {
                    next = CA_FLOOR;
                } else {
                    next = CA_WALL;
                }
            }

            scratch[ca_idx(x, y, width)] = next;
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = ca_idx(x, y, width);
            ca->grid[idx] = scratch[idx];
        }
    }

    ca->cur_iter++;
}

/* --- UNION-FIND (for find_regions) --------------------------------------- */

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

/* --- REGION LABELLING ---------------------------------------------------- */

static int
find_regions(rlk_wg_ca_t *ca) {
    assert(ca);
    assert(ca->grid);
    assert(ca->region_ids);

    int width = ca->width;
    int height = ca->height;
    size_t grid_size = (size_t)width * (size_t)height;

    /* Phase 1: init parent[i] = i for floor, -1 for wall. */
    for (size_t i = 0; i < grid_size; i++) {
        ca->region_ids[i] = (ca->grid[i] == CA_FLOOR) ? (int)i : -1;
    }

    /* Phase 2: union neighboring floor cells (8-connectivity).
       Forward neighbors cover all pairs: right, down, down-right, down-left. */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = ca_idx(x, y, width);
            if (ca->grid[idx] != CA_FLOOR) continue;

            if (x + 1 < width) {
                size_t ridx = idx + 1;
                if (ca->grid[ridx] == CA_FLOOR)
                    uf_union(ca->region_ids, (int)idx, (int)ridx);
            }
            if (y + 1 < height) {
                size_t bidx = idx + (size_t)width;
                if (ca->grid[bidx] == CA_FLOOR)
                    uf_union(ca->region_ids, (int)idx, (int)bidx);
            }
            if (x + 1 < width && y + 1 < height) {
                size_t dridx = idx + (size_t)width + 1;
                if (ca->grid[dridx] == CA_FLOOR)
                    uf_union(ca->region_ids, (int)idx, (int)dridx);
            }
            if (x > 0 && y + 1 < height) {
                size_t dlidx = idx + (size_t)width - 1;
                if (ca->grid[dlidx] == CA_FLOOR)
                    uf_union(ca->region_ids, (int)idx, (int)dlidx);
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
find_region_center(const rlk_wg_ca_t *ca, int region_id, int *center_x, int *center_y) {
    assert(ca);
    assert(ca->region_ids);
    assert(region_id >= 1 && region_id <= ca->region_count);

    int width = ca->width;
    int height = ca->height;
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

/* Carve corridors to connect all floor regions. Assumes find_regions() was
   called: ca->region_ids and ca->region_count must be valid. */
static void
connect_regions(rlk_wg_ca_t *ca) {
    assert(ca);
    assert(ca->region_ids);

    int region_count = ca->region_count;
    if (region_count <= 1) {
        return;
    }

    int width = ca->width;
    int height = ca->height;

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

        /* Find closest pair of cells between region i and nearest_j */
        int best_x1 = -1, best_y1 = -1, best_x2 = -1, best_y2 = -1;
        int best_dist = INT_MAX;

        for (int y1 = 0; y1 < height; y1++) {
            for (int x1 = 0; x1 < width; x1++) {
                size_t idx1 = ca_idx(x1, y1, width);
                if (ca->region_ids[idx1] != i) continue;

                for (int y2 = 0; y2 < height; y2++) {
                    for (int x2 = 0; x2 < width; x2++) {
                        size_t idx2 = ca_idx(x2, y2, width);
                        if (ca->region_ids[idx2] != nearest_j) continue;

                        int dist = rlk_dist_manhattan(x1, y1, x2, y2);
                        if (dist < best_dist) {
                            best_dist = dist;
                            best_x1 = x1;
                            best_y1 = y1;
                            best_x2 = x2;
                            best_y2 = y2;
                        }
                    }
                }
            }
        }

        if (best_x1 != -1 && best_x2 != -1) {
            int x = best_x1;
            int y = best_y1;
            int dx = (best_x2 > best_x1) ? 1 : ((best_x2 < best_x1) ? -1 : 0);
            int dy = (best_y2 > best_y1) ? 1 : ((best_y2 < best_y1) ? -1 : 0);

            while (x != best_x2 || y != best_y2) {
                if (x != best_x2) {
                    if (dx > 0) x++;
                    else if (dx < 0) x--;
                }
                if (y != best_y2) {
                    if (dy > 0) y++;
                    else if (dy < 0) y--;
                }

                ca->grid[ca_idx(x, y, width)] = CA_FLOOR;
            }

            ca->grid[ca_idx(best_x2, best_y2, width)] = CA_FLOOR;
        }
    }
}

/* --- PUBLIC API ---------------------------------------------------------- */

void
rlk_wg_ca_fill(rlk_wg_ca_t *ca, int ox, int oy, rlk_tilemap_t *tm) {
    assert(ca);
    assert(tm);

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

    /* Render at offset (ox, oy). rlk_tilemap_get clips out-of-bounds. */
    for (int y = 0; y < ca->height; y++) {
        for (int x = 0; x < ca->width; x++) {
            rlk_tile_t *tile = rlk_tilemap_get(tm, ox + x, oy + y);
            if (tile) {
                size_t idx = ca_idx(x, y, ca->width);
                uint8_t cell = ca->grid[idx];
                int rid = (cell == CA_FLOOR) ? ca->region_ids[idx] : 0;
                /* Preset biome to region id, then the writer fills type/flags/variant. */
                *tile = (rlk_tile_t){.biome = (uint8_t)rid};
                ca->write(ca, ca->write_data, x, y, tile);
            }
        }
    }
}
