#include "world.h"

#include <assert.h>
#include <stdio.h>

/* Save format: magic, version, seed, chunk, ndiffs, then per diff
   (wx, wy, tile). Native-endian, same-machine. */
#define SAVE_MAGIC   0x524C4B57u   /* "RLKW" */
#define SAVE_VERSION 1u

void
rlk_world_init(rlk_world_t *w, int chunk, rlk_world_diff_t *diffs, int diff_cap) {
    assert(w && chunk > 0 && diffs && diff_cap > 0);
    w->chunk   = chunk;
    w->diffs   = diffs;
    w->ndiffs  = 0;
    w->diff_cap = diff_cap;
    rlk_wg_init(&w->wg);
}

float
rlk_world_field(const rlk_world_t *w, int idx, int wx, int wy) {
    assert(w);
    return rlk_wg_sample_field(&w->wg, idx, (float)wx + 0.5f, (float)wy + 0.5f);
}

/* Overwrite *tile with the diff recorded at (wx, wy), if any. */
static void
apply_diff(const rlk_world_t *w, int wx, int wy, rlk_tile_t *tile) {
    assert(w && tile);
    for (int i = 0; i < w->ndiffs; i++)
        if (w->diffs[i].wx == wx && w->diffs[i].wy == wy) {
            *tile = w->diffs[i].tile;
            return;
        }
}

void
rlk_world_fill(const rlk_world_t *w, int cx, int cy, rlk_tilemap_t *tm) {
    assert(w && tm);
    assert(tm->w == w->chunk && tm->h == w->chunk);
    const int ox = cx * w->chunk, oy = cy * w->chunk;
    tm->origin_x = ox;
    tm->origin_y = oy;
    rlk_wg_fill(&w->wg, ox, oy, tm);
    for (int i = 0; i < w->ndiffs; i++) {
        const rlk_world_diff_t *d = &w->diffs[i];
        if (d->wx >= ox && d->wx < ox + w->chunk &&
            d->wy >= oy && d->wy < oy + w->chunk)
            rlk_tilemap_set(tm, d->wx, d->wy, d->tile);
    }
}

void
rlk_world_tile(const rlk_world_t *w, int wx, int wy, rlk_tile_t *out) {
    assert(w && out);
    rlk_tile_t buf;
    rlk_tilemap_t tm = { .w = 1, .h = 1, .origin_x = wx, .origin_y = wy,
                         .tiles = &buf };
    rlk_wg_fill(&w->wg, wx, wy, &tm);
    apply_diff(w, wx, wy, &buf);
    *out = buf;
}

rlk_err_t
rlk_world_set(rlk_world_t *w, int wx, int wy, rlk_tile_t tile) {
    assert(w);
    for (int i = 0; i < w->ndiffs; i++)
        if (w->diffs[i].wx == wx && w->diffs[i].wy == wy) {
            w->diffs[i].tile = tile;
            return RLK_OK;
        }
    if (w->ndiffs >= w->diff_cap)
        return RLK_E_INVALID;
    w->diffs[w->ndiffs++] = (rlk_world_diff_t){ .wx = wx, .wy = wy, .tile = tile };
    return RLK_OK;
}

rlk_err_t
rlk_world_find(const rlk_world_t *w, rlk_rect_t area, rlk_world_pred_fn pred,
               void *data, int *out_wx, int *out_wy) {
    assert(w && pred && out_wx && out_wy);
    assert(area.w > 0 && area.h > 0);
    for (int y = area.y; y < area.y + area.h; y++)
        for (int x = area.x; x < area.x + area.w; x++) {
            rlk_tile_t t;
            rlk_world_tile(w, x, y, &t);
            if (pred(w, x, y, &t, data)) {
                *out_wx = x;
                *out_wy = y;
                return RLK_OK;
            }
        }
    return RLK_E_NOTFOUND;
}

static int
write_all(FILE *f, const void *p, size_t n) {
    return fwrite(p, 1, n, f) == n;
}

static int
read_all(FILE *f, void *p, size_t n) {
    return fread(p, 1, n, f) == n;
}

rlk_err_t
rlk_world_save(const rlk_world_t *w, FILE *f) {
    assert(w && f);
    const uint32_t magic = SAVE_MAGIC, version = SAVE_VERSION;
    const uint32_t nd = (uint32_t)w->ndiffs;
    const uint64_t seed = w->wg.seed;
    const int32_t chunk = w->chunk;
    if (!write_all(f, &magic, sizeof magic) ||
        !write_all(f, &version, sizeof version) ||
        !write_all(f, &seed, sizeof seed) ||
        !write_all(f, &chunk, sizeof chunk) ||
        !write_all(f, &nd, sizeof nd))
        return RLK_E_INVALID;
    for (int i = 0; i < w->ndiffs; i++) {
        const int32_t wx = w->diffs[i].wx, wy = w->diffs[i].wy;
        if (!write_all(f, &wx, sizeof wx) ||
            !write_all(f, &wy, sizeof wy) ||
            !write_all(f, &w->diffs[i].tile, sizeof(rlk_tile_t)))
            return RLK_E_INVALID;
    }
    return RLK_OK;
}

rlk_err_t
rlk_world_load(rlk_world_t *w, FILE *f) {
    assert(w && f);
    w->ndiffs = 0;   /* on failure the diff list is left empty */
    uint32_t magic, version, nd;
    uint64_t seed;
    int32_t chunk;
    if (!read_all(f, &magic, sizeof magic) || magic != SAVE_MAGIC ||
        !read_all(f, &version, sizeof version) || version != SAVE_VERSION ||
        !read_all(f, &seed, sizeof seed) ||
        !read_all(f, &chunk, sizeof chunk) ||
        !read_all(f, &nd, sizeof nd))
        return RLK_E_INVALID;
    if (seed != w->wg.seed || chunk != w->chunk || (int)nd > w->diff_cap)
        return RLK_E_INVALID;
    for (uint32_t i = 0; i < nd; i++) {
        int32_t wx, wy;
        rlk_tile_t tile;
        if (!read_all(f, &wx, sizeof wx) ||
            !read_all(f, &wy, sizeof wy) ||
            !read_all(f, &tile, sizeof tile))
            return RLK_E_INVALID;
        w->diffs[i] = (rlk_world_diff_t){ .wx = wx, .wy = wy, .tile = tile };
    }
    w->ndiffs = (int)nd;
    return RLK_OK;
}
