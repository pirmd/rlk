#include "bsp.h"
#include "hash.h"

#include <string.h>

/* Internal cell states for the BSP grid buffer. */
enum { BSP_WALL = 0, BSP_FLOOR = 1 };

static inline size_t
bsp_idx(int x, int y, int w) {
    return (size_t)y * (size_t)w + (size_t)x;
}

/* --- SETUP --------------------------------------------------------------- */

void
rlk_bsp_init(rlk_bsp_t *bsp) {
    assert(bsp);

    /* Resolve zero defaults. */
    if (bsp->min_room_size == 0)
        bsp->min_room_size = 5;
    if (bsp->max_depth == 0)
        bsp->max_depth = 7;
    if (bsp->split_bias == 0.0f)
        bsp->split_bias = 0.5f;
    if (bsp->room_margin == 0)
        bsp->room_margin = 1;

    /* Safety checks (programmer errors): NULL buffers/callback, and config
       ranges after default resolution. */
    assert(bsp->grid && bsp->nodes);
    assert(bsp->write);
    assert(bsp->min_room_size >= 1);
    assert(bsp->max_depth >= 1);
    assert(bsp->split_bias >= 0.0f && bsp->split_bias <= 1.0f);
    assert(bsp->room_margin >= 0);
}

/* --- RANDOM -------------------------------------------------------------- */

/* Deterministic float in [0,1) from seed + node index + depth + salt. */
static float
bsp_rand(const rlk_bsp_t *bsp, int node_idx, int depth, uint64_t salt) {
    assert(bsp);
    return rlk_hash_normf(rlk_hash_at(bsp->seed, node_idx, depth, salt));
}

/* Deterministic int in [lo, hi] from seed + node index + depth + salt. */
static int
bsp_rand_int(const rlk_bsp_t *bsp, int node_idx, int depth,
             uint64_t salt, int lo, int hi) {
    assert(lo <= hi);
    float r = bsp_rand(bsp, node_idx, depth, salt);
    return lo + (int)(r * (float)(hi - lo + 1));
}

/* --- CARVING ------------------------------------------------------------- */

static void
carve_rect(rlk_bsp_t *bsp, int x, int y, int w, int h) {
    assert(bsp);
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int cx = x + dx;
            int cy = y + dy;
            if (cx >= 0 && cx < bsp->win_w && cy >= 0 && cy < bsp->win_h)
                bsp->grid[bsp_idx(cx, cy, bsp->win_w)] = BSP_FLOOR;
        }
    }
}

static void
carve_h(rlk_bsp_t *bsp, int x1, int x2, int y) {
    assert(bsp);
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    for (int x = x1; x <= x2; x++) {
        if (x >= 0 && x < bsp->win_w && y >= 0 && y < bsp->win_h)
            bsp->grid[bsp_idx(x, y, bsp->win_w)] = BSP_FLOOR;
    }
}

static void
carve_v(rlk_bsp_t *bsp, int x, int y1, int y2) {
    assert(bsp);
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; y++) {
        if (x >= 0 && x < bsp->win_w && y >= 0 && y < bsp->win_h)
            bsp->grid[bsp_idx(x, y, bsp->win_w)] = BSP_FLOOR;
    }
}

/* --- BSP TREE ------------------------------------------------------------ */

/* Recursively split a node. */
static void
split_recursive(rlk_bsp_t *bsp, int node_idx, int depth) {
    assert(bsp);
    assert(node_idx >= 0 && node_idx < RLK_BSP_MAX_NODES);

    rlk_bsp_node_t *node = &bsp->nodes[node_idx];
    int w = node->w;
    int h = node->h;
    int min_sz = bsp->min_room_size;

    /* Stop splitting if too deep or too small. */
    int can_split_h = (h >= 2 * min_sz);
    int can_split_v = (w >= 2 * min_sz);

    if (depth >= bsp->max_depth || (!can_split_h && !can_split_v)) {
        node->split = RLK_BSP_LEAF;
        node->left = -1;
        node->right = -1;
        return;
    }

    /* Both branches below allocate two child nodes. */
    assert(bsp->node_count + 2 <= RLK_BSP_MAX_NODES);

    /* Choose split direction. */
    int do_horizontal;
    if (!can_split_h)      do_horizontal = 0;
    else if (!can_split_v) do_horizontal = 1;
    else {
        float r = bsp_rand(bsp, node_idx, depth, 0x5011ULL);
        do_horizontal = (r < bsp->split_bias);
    }

    /* Split position and the two child rectangles. */
    int max_split = (do_horizontal ? h : w) - min_sz;
    int pos = bsp_rand_int(bsp, node_idx, depth, 0x5012ULL, min_sz, max_split);
    rlk_bsp_node_t child[2];
    if (do_horizontal) {
        /* Split along Y: top and bottom children. */
        node->split = RLK_BSP_SPLIT_H;
        child[0] = (rlk_bsp_node_t){ .x = node->x, .y = node->y,
                                     .w = node->w, .h = pos };
        child[1] = (rlk_bsp_node_t){ .x = node->x, .y = node->y + pos,
                                     .w = node->w, .h = node->h - pos };
    } else {
        /* Split along X: left and right children. */
        node->split = RLK_BSP_SPLIT_V;
        child[0] = (rlk_bsp_node_t){ .x = node->x, .y = node->y,
                                     .w = pos, .h = node->h };
        child[1] = (rlk_bsp_node_t){ .x = node->x + pos, .y = node->y,
                                     .w = node->w - pos, .h = node->h };
    }
    node->split_pos = pos;

    int left_idx = bsp->node_count++;
    int right_idx = bsp->node_count++;
    node->left = left_idx;
    node->right = right_idx;
    bsp->nodes[left_idx] = child[0];
    bsp->nodes[right_idx] = child[1];

    split_recursive(bsp, left_idx, depth + 1);
    split_recursive(bsp, right_idx, depth + 1);
}

/* Create a room inside a leaf node, with random inset. */
static void
make_room(rlk_bsp_t *bsp, int node_idx) {
    assert(bsp);
    rlk_bsp_node_t *node = &bsp->nodes[node_idx];
    assert(node->split == RLK_BSP_LEAF);

    int margin = bsp->room_margin;
    int max_inset = margin + 2;

    /* Random inset for each edge, clamped so room stays >= min_room_size. */
    int max_x_inset = (node->w - bsp->min_room_size) / 2;
    int max_y_inset = (node->h - bsp->min_room_size) / 2;
    if (max_x_inset < margin) max_x_inset = margin;
    if (max_y_inset < margin) max_y_inset = margin;
    if (max_x_inset > max_inset) max_x_inset = max_inset;
    if (max_y_inset > max_inset) max_y_inset = max_inset;

    int inset_l = bsp_rand_int(bsp, node_idx, 0, 0xB001ULL, margin, max_x_inset);
    int inset_r = bsp_rand_int(bsp, node_idx, 0, 0xB002ULL, margin, max_x_inset);
    int inset_t = bsp_rand_int(bsp, node_idx, 0, 0xB003ULL, margin, max_y_inset);
    int inset_b = bsp_rand_int(bsp, node_idx, 0, 0xB004ULL, margin, max_y_inset);

    node->rx = node->x + inset_l;
    node->ry = node->y + inset_t;
    node->rw = node->w - inset_l - inset_r;
    node->rh = node->h - inset_t - inset_b;

    /* Clamp to min size. */
    if (node->rw < bsp->min_room_size) node->rw = bsp->min_room_size;
    if (node->rh < bsp->min_room_size) node->rh = bsp->min_room_size;

    bsp->room_count++;
}

/* Carve a room into the grid. */
static void
carve_room(rlk_bsp_t *bsp, int node_idx) {
    assert(bsp);
    rlk_bsp_node_t *node = &bsp->nodes[node_idx];
    carve_rect(bsp, node->rx, node->ry, node->rw, node->rh);
}

/* Connect two rooms with an L-shaped corridor. */
static void
connect_rooms(rlk_bsp_t *bsp, int left_idx, int right_idx) {
    assert(bsp);
    rlk_bsp_node_t *l = &bsp->nodes[left_idx];
    rlk_bsp_node_t *r = &bsp->nodes[right_idx];

    /* Room centers. */
    int lcx = l->rx + l->rw / 2;
    int lcy = l->ry + l->rh / 2;
    int rcx = r->rx + r->rw / 2;
    int rcy = r->ry + r->rh / 2;

    /* Randomly choose corridor shape: horizontal-first or vertical-first. */
    float shape = bsp_rand(bsp, left_idx, 0, 0xC001ULL);

    if (shape < 0.5f) {
        /* Horizontal first, then vertical. */
        carve_h(bsp, lcx, rcx, lcy);
        carve_v(bsp, rcx, lcy, rcy);
    } else {
        /* Vertical first, then horizontal. */
        carve_v(bsp, lcx, lcy, rcy);
        carve_h(bsp, lcx, rcx, rcy);
    }
}

/* Return the index of the first leaf in the subtree rooted at node_idx. */
static int
first_leaf_idx(const rlk_bsp_t *bsp, int node_idx) {
    assert(bsp);
    const rlk_bsp_node_t *node = &bsp->nodes[node_idx];
    if (node->split == RLK_BSP_LEAF) return node_idx;
    return first_leaf_idx(bsp, node->left);
}

/* Recursively carve rooms and connect siblings. */
static void
build_dungeon(rlk_bsp_t *bsp, int node_idx) {
    assert(bsp);
    rlk_bsp_node_t *node = &bsp->nodes[node_idx];

    if (node->split == RLK_BSP_LEAF) {
        make_room(bsp, node_idx);
        carve_room(bsp, node_idx);
        return;
    }

    build_dungeon(bsp, node->left);
    build_dungeon(bsp, node->right);
    /* Connect the first leaf of each subtree. Since each subtree is
       internally connected, this joins the two halves. */
    connect_rooms(bsp, first_leaf_idx(bsp, node->left),
                  first_leaf_idx(bsp, node->right));
}

/* --- PUBLIC API ---------------------------------------------------------- */

void
rlk_bsp_fill(rlk_bsp_t *bsp, const rlk_lattice_t *win, rlk_tilemap_t *tm,
             const rlk_mask_t *mask, const rlk_poi_list_t *poi,
             rlk_poi_t *spawn) {
    assert(bsp && win && tm);
    assert(win->w > 0 && win->h > 0 && win->cellsize > 0);
    assert(bsp->grid && bsp->nodes && bsp->write);
    if (mask)
        assert(rlk_lattice_same(&mask->lattice, win));

    const rlk_lattice_t *l = win;
    const int w = l->w, h = l->h, cs = l->cellsize;

    /* The tilemap must fully cover the window: caller contract. Each tile
       write is bounds-safe, but a partial window would silently truncate
       the fill, so check it once here. */
    assert(rlk_tilemap_contains_lattice(tm, l));

    bsp->win_w = w;
    bsp->win_h = h;

    /* Clear grid to walls. */
    memset(bsp->grid, BSP_WALL, (size_t)w * (size_t)h);

    /* Build BSP tree: root node covers the full grid. */
    bsp->node_count = 0;
    bsp->room_count = 0;

    int root_idx = bsp->node_count++;
    bsp->nodes[root_idx] = (rlk_bsp_node_t){
        .x = 0, .y = 0,
        .w = w, .h = h,
    };

    split_recursive(bsp, root_idx, 0);
    build_dungeon(bsp, root_idx);

    /* Render: writer per cell, stamped over the cell's cs x cs tile block.
       Mask weight 0 and reserved POI bboxes are not written. */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t idx = bsp_idx(x, y, w);
            if (mask && mask->weights[idx] == 0)
                continue;
            const int wx = l->origin_x + x * cs;
            const int wy = l->origin_y + y * cs;
            uint8_t cell = bsp->grid[idx];
            /* Preset biome to cell type, then the writer fills type/flags/variant. */
            rlk_tile_t tile = { .biome = cell };
            bsp->write(bsp->seed, bsp->write_data, wx, wy, &tile);
            for (int dy = 0; dy < cs; dy++) {
                for (int dx = 0; dx < cs; dx++) {
                    const int tx = wx + dx, ty = wy + dy;
                    if (!poi || !rlk_poi_at(poi, tx, ty))
                        rlk_tilemap_set(tm, tx, ty, tile);
                }
            }
        }
    }

    /* Entrance POI: centre of the largest room. */
    if (spawn && bsp->room_count > 0) {
        int best = -1, best_area = -1;
        for (int i = 0; i < bsp->node_count; i++) {
            const rlk_bsp_node_t *n = &bsp->nodes[i];
            if (n->split != RLK_BSP_LEAF)
                continue;
            int area = n->rw * n->rh;
            if (area > best_area) {
                best_area = area;
                best = i;
            }
        }
        if (best >= 0) {
            const rlk_bsp_node_t *n = &bsp->nodes[best];
            spawn->x = l->origin_x + (n->rx + n->rw / 2) * cs + cs / 2;
            spawn->y = l->origin_y + (n->ry + n->rh / 2) * cs + cs / 2;
            spawn->bbox = (rlk_rect_t){
                .x = l->origin_x + n->rx * cs, .y = l->origin_y + n->ry * cs,
                .w = n->rw * cs, .h = n->rh * cs,
            };
            spawn->seed = rlk_poi_seed(bsp->seed, spawn->x, spawn->y, 0, 0x5EEDULL);
        }
    }
}
