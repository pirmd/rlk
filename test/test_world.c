#include <assert.h>
#include <stdio.h>

#include "world.h"

#define CHUNK 8

static void
zone_write(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->type = ctx->zone;
}

static void
make_world(rlk_world_t *w, uint64_t seed, int chunk,
           rlk_world_diff_t *diffs, int cap) {
    *w = (rlk_world_t){0};
    w->wg.seed = seed;
    w->wg.nsampled = 2;
    w->wg.fields[0] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 11, .freq = 0.10f };
    w->wg.fields[1] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 23, .freq = 0.23f };
    w->wg.naxes = 1;
    w->wg.axes[0] = (rlk_wg_axis_t){ .slot = 0, .jitter_slot = RLK_WG_NONE,
                                     .jitter_amp = 0.0f };
    w->wg.nzones = 2;
    for (int i = 0; i < 2; i++) {
        rlk_wg_zone_t *z = &w->wg.zones[i];
        z->write = zone_write;
        z->lo[0] = -RLK_WG_AXIS_UNBOUNDED;
        z->hi[0] =  RLK_WG_AXIS_UNBOUNDED;
    }
    w->wg.zones[0].hi[0] = 0.5f;
    w->wg.zones[1].lo[0] = 0.5f;
    w->wg.inside_scale = 0.05f;
    w->wg.border_width = 0.05f;
    rlk_world_init(w, chunk, diffs, cap);
}

/* field: same value as the wg sampler at the tile centre; deterministic
   per seed; a different seed decorrelates. */

static void
test_field_matches_wg(void) {
    rlk_world_diff_t da[4], db[4], dc[4];
    rlk_world_t a, b, c;
    make_world(&a, 42, CHUNK, da, 4);
    make_world(&b, 42, CHUNK, db, 4);
    make_world(&c, 43, CHUNK, dc, 4);

    for (int y = -3; y < 5; y++)
        for (int x = -3; x < 5; x++)
            assert(rlk_world_field(&a, 0, x, y) ==
                   rlk_wg_sample_field(&a.wg, 0, (float)x + 0.5f, (float)y + 0.5f));

    int differ = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            assert(rlk_world_field(&a, 1, x, y) == rlk_world_field(&b, 1, x, y));
            if (rlk_world_field(&a, 0, x, y) != rlk_world_field(&c, 0, x, y))
                differ = 1;
        }
    assert(differ);
}

/* fill: origin set to the chunk origin; same tiles as a direct wg fill at
   the same origin; a tile has the same value whatever chunk contains it. */

static void
test_fill_chunk(void) {
    rlk_world_diff_t diffs[4];
    rlk_world_t w;
    make_world(&w, 42, CHUNK, diffs, 4);

    static rlk_tile_t tiles[CHUNK * CHUNK], ref[CHUNK * CHUNK];
    rlk_tilemap_t tm, tm2;
    rlk_tilemap_init(&tm, CHUNK, CHUNK, tiles, sizeof tiles);
    rlk_world_fill(&w, 2, -1, &tm);
    assert(tm.origin_x == 2 * CHUNK && tm.origin_y == -CHUNK);

    rlk_tilemap_init(&tm2, CHUNK, CHUNK, ref, sizeof ref);
    tm2.origin_x = 2 * CHUNK;
    tm2.origin_y = -CHUNK;
    rlk_wg_fill(&w.wg, 2 * CHUNK, -CHUNK, &tm2);
    for (int k = 0; k < CHUNK * CHUNK; k++)
        assert(tiles[k].type == ref[k].type && tiles[k].biome == ref[k].biome);

    /* seam coherence: compare chunk fills against one wide direct fill */
    static rlk_tile_t wide[2 * CHUNK * CHUNK], c0[CHUNK * CHUNK], c1[CHUNK * CHUNK];
    rlk_tilemap_t tmw, t0, t1;
    rlk_tilemap_init(&tmw, 2 * CHUNK, CHUNK, wide, sizeof wide);
    rlk_wg_fill(&w.wg, 0, 0, &tmw);
    rlk_tilemap_init(&t0, CHUNK, CHUNK, c0, sizeof c0);
    rlk_tilemap_init(&t1, CHUNK, CHUNK, c1, sizeof c1);
    rlk_world_fill(&w, 0, 0, &t0);
    rlk_world_fill(&w, 1, 0, &t1);
    for (int y = 0; y < CHUNK; y++)
        for (int x = 0; x < CHUNK; x++) {
            const rlk_tile_t *a = rlk_tilemap_get(&tmw, x, y);
            const rlk_tile_t *b = rlk_tilemap_get(&t0, x, y);
            const rlk_tile_t *c = rlk_tilemap_get(&tmw, CHUNK + x, y);
            const rlk_tile_t *d = rlk_tilemap_get(&t1, CHUNK + x, y);
            assert(a && b && c && d);
            assert(a->type == b->type && c->type == d->type);
        }
}

/* tile: the per-tile primitive matches the chunk fill at the same world
   position (including negative coordinates). */

static void
test_tile(void) {
    rlk_world_diff_t diffs[4];
    rlk_world_t w;
    make_world(&w, 42, CHUNK, diffs, 4);

    static rlk_tile_t tiles[CHUNK * CHUNK];
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, CHUNK, CHUNK, tiles, sizeof tiles);
    rlk_world_fill(&w, -1, 2, &tm);
    for (int y = 0; y < CHUNK; y += 3)
        for (int x = 0; x < CHUNK; x += 3) {
            const int wx = -CHUNK + x, wy = 2 * CHUNK + y;
            rlk_tile_t t;
            rlk_world_tile(&w, wx, wy, &t);
            const rlk_tile_t *p = rlk_tilemap_get(&tm, wx, wy);
            assert(p && t.type == p->type && t.biome == p->biome);
        }
}

/* set: the diff is applied on every regeneration of the chunk and by the
   per-tile primitive; re-set replaces; a diff stays in its own chunk. */

static void
test_diff(void) {
    rlk_world_diff_t diffs[4];
    rlk_world_t w;
    make_world(&w, 42, CHUNK, diffs, 4);

    const rlk_tile_t m1 = { .type = 200, .flags = 1, .biome = 2, .variant = 3, .depth = 4 };
    assert(rlk_world_set(&w, 3, 4, m1) == RLK_OK);
    assert(w.ndiffs == 1);

    static rlk_tile_t tiles[CHUNK * CHUNK];
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, CHUNK, CHUNK, tiles, sizeof tiles);
    rlk_world_fill(&w, 0, 0, &tm);   /* regenerate */
    rlk_world_fill(&w, 0, 0, &tm);   /* regenerate again: diff reapplied */
    const rlk_tile_t *p = rlk_tilemap_get(&tm, 3, 4);
    assert(p && p->type == 200 && p->flags == 1 && p->biome == 2 &&
           p->variant == 3 && p->depth == 4);

    rlk_tile_t t;
    rlk_world_tile(&w, 3, 4, &t);
    assert(t.type == 200 && t.depth == 4);

    /* re-set replaces the payload, no new entry */
    const rlk_tile_t m2 = { .type = 201 };
    assert(rlk_world_set(&w, 3, 4, m2) == RLK_OK);
    assert(w.ndiffs == 1);
    rlk_world_fill(&w, 0, 0, &tm);
    p = rlk_tilemap_get(&tm, 3, 4);
    assert(p && p->type == 201);

    /* a diff in another chunk applies there, not here */
    assert(rlk_world_set(&w, 30, 30, m1) == RLK_OK);
    assert(w.ndiffs == 2);
    rlk_world_fill(&w, 3, 3, &tm);
    p = rlk_tilemap_get(&tm, 30, 30);
    assert(p && p->type == 200);
    rlk_world_fill(&w, 0, 0, &tm);
    p = rlk_tilemap_get(&tm, 3, 4);
    assert(p && p->type == 201);
}

/* set: a full diff list returns RLK_E_INVALID. */

static void
test_diff_cap(void) {
    rlk_world_diff_t diffs[1];
    rlk_world_t w;
    make_world(&w, 42, CHUNK, diffs, 1);
    const rlk_tile_t m = { .type = 200 };
    assert(rlk_world_set(&w, 0, 0, m) == RLK_OK);
    assert(rlk_world_set(&w, 1, 1, m) == RLK_E_INVALID);
    assert(w.ndiffs == 1);
}

static int
pred_all(const rlk_world_t *w, int wx, int wy, const rlk_tile_t *tile, void *data) {
    (void)w; (void)wx; (void)wy; (void)tile; (void)data;
    return 1;
}

static int
pred_none(const rlk_world_t *w, int wx, int wy, const rlk_tile_t *tile, void *data) {
    (void)w; (void)wx; (void)wy; (void)tile; (void)data;
    return 0;
}

static int
pred_field_high(const rlk_world_t *w, int wx, int wy, const rlk_tile_t *tile, void *data) {
    (void)tile; (void)data;
    return rlk_world_field(w, 0, wx, wy) > 0.5f;
}

/* find: row-major first match, NOTFOUND when nothing matches, predicate can
   read the macro fields. */

static void
test_find(void) {
    rlk_world_diff_t diffs[4];
    rlk_world_t w;
    make_world(&w, 42, CHUNK, diffs, 4);

    const rlk_rect_t area = { .x = -8, .y = -8, .w = 16, .h = 16 };
    int ox, oy;
    assert(rlk_world_find(&w, area, pred_all, NULL, &ox, &oy) == RLK_OK);
    assert(ox == area.x && oy == area.y);
    assert(rlk_world_find(&w, area, pred_none, NULL, &ox, &oy) == RLK_E_NOTFOUND);
    assert(rlk_world_find(&w, area, pred_field_high, NULL, &ox, &oy) == RLK_OK);
    assert(rlk_world_field(&w, 0, ox, oy) > 0.5f);
}

/* save/load: (seed + chunk + diffs) round-trips, loaded diffs apply on
   regeneration; a seed mismatch or a truncated file is rejected. */

static void
test_save_load(void) {
    rlk_world_diff_t da[8], db[8], dc[8];
    rlk_world_t a, b, c;
    make_world(&a, 42, CHUNK, da, 8);
    make_world(&b, 42, CHUNK, db, 8);
    make_world(&c, 43, CHUNK, dc, 8);

    const rlk_tile_t m1 = { .type = 200, .variant = 7 };
    const rlk_tile_t m2 = { .type = 201, .flags = 9 };
    assert(rlk_world_set(&a, 3, 4, m1) == RLK_OK);
    assert(rlk_world_set(&a, -5, 40, m2) == RLK_OK);

    FILE *f = tmpfile();
    assert(f);
    assert(rlk_world_save(&a, f) == RLK_OK);
    rewind(f);
    assert(rlk_world_load(&b, f) == RLK_OK);
    assert(b.ndiffs == 2);
    assert(b.diffs[0].wx == 3 && b.diffs[0].wy == 4 &&
           b.diffs[0].tile.type == 200 && b.diffs[0].tile.variant == 7);
    assert(b.diffs[1].wx == -5 && b.diffs[1].wy == 40 &&
           b.diffs[1].tile.type == 201 && b.diffs[1].tile.flags == 9);

    static rlk_tile_t tiles[CHUNK * CHUNK];
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, CHUNK, CHUNK, tiles, sizeof tiles);
    rlk_world_fill(&b, 0, 0, &tm);
    const rlk_tile_t *p = rlk_tilemap_get(&tm, 3, 4);
    assert(p && p->type == 200);
    rlk_world_fill(&b, -1, 5, &tm);   /* chunk of (-5, 40) */
    p = rlk_tilemap_get(&tm, -5, 40);
    assert(p && p->type == 201);

    rewind(f);
    assert(rlk_world_load(&c, f) == RLK_E_INVALID);   /* seed mismatch */
    assert(c.ndiffs == 0);
    fclose(f);

    FILE *f2 = tmpfile();
    assert(f2);
    assert(fwrite("XXXX", 1, 4, f2) == 4);
    rewind(f2);
    assert(rlk_world_load(&b, f2) == RLK_E_INVALID);  /* truncated/corrupt */
    assert(b.ndiffs == 0);                             /* left empty on failure */
    fclose(f2);
}

/* Banner example of rlk_world_fill. */

static void
test_example_rlk_world_fill(void) {
    static rlk_world_diff_t diff_buf[256];
    static rlk_tile_t tiles[32 * 32];
    rlk_world_t w = {0};
    w.wg.seed = 42;
    w.wg.nsampled = 1;
    w.wg.fields[0] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 1, .freq = 0.1f };
    w.wg.naxes = 1;
    w.wg.axes[0] = (rlk_wg_axis_t){ .slot = 0, .jitter_slot = RLK_WG_NONE,
                                    .jitter_amp = 0.0f };
    w.wg.nzones = 1;
    w.wg.zones[0].write = zone_write;
    w.wg.zones[0].lo[0] = -RLK_WG_AXIS_UNBOUNDED;
    w.wg.zones[0].hi[0] =  RLK_WG_AXIS_UNBOUNDED;
    w.wg.inside_scale = 0.05f;
    w.wg.border_width = 0.05f;
    rlk_world_init(&w, 32, diff_buf, 256);
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, 32, 32, tiles, sizeof tiles);
    rlk_world_fill(&w, 3, -1, &tm);
    assert(tm.origin_x == 96 && tm.origin_y == -32);
    assert(rlk_tilemap_get(&tm, 96, -32) != NULL);
}

int
main(void) {
    test_field_matches_wg();
    test_fill_chunk();
    test_tile();
    test_diff();
    test_diff_cap();
    test_find();
    test_save_load();
    test_example_rlk_world_fill();
    return 0;
}
