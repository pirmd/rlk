#include <assert.h>
#include <math.h>
#include <stdint.h>
#include "wg.h"

#define MAP 64

static rlk_tile_t tilebuf1[MAP * MAP];
static rlk_tile_t tilebuf2[MAP * MAP];

/* Writers --------------------------------------------------------------------- */

static void
write_simple(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->type = (uint8_t)ctx->zone;
    out->biome = (uint8_t)ctx->zone;
}

/* Biome + clamped field[0] quantized into variant. */
static void
write_field0(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->biome = (uint8_t)ctx->zone;
    out->variant = (uint8_t)(rlk_clampf(ctx->field[0], 0.0f, 1.0f) * 255.0f);
}

/* field[0..2] quantized into type/variant/flags + biome. */
static void
write_fields012(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->type = (uint8_t)(ctx->field[0] * 255.0f);
    out->variant = (uint8_t)(ctx->field[1] * 255.0f);
    out->flags = (uint8_t)(ctx->field[2] * 255.0f);
    out->biome = (uint8_t)ctx->zone;
}

/* Capture ctx for tests that inspect pipeline internals. */
static rlk_wg_ctx_t g_ctx;
static int g_ctx_count;

static void
write_capture(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    g_ctx = *ctx;
    g_ctx_count++;
    out->type = (uint8_t)ctx->zone;
    out->biome = (uint8_t)ctx->zone;
}

/* Stages ---------------------------------------------------------------------- */

/* Overwrite field[0] with the constant pointed to by `data`. */
static void
stage_const_field0(rlk_wg_ctx_t *ctx, const void *data) {
    ctx->field[0] = *(const float *)data;
}

/* Overwrite field[2] with the constant pointed to by `data`. */
static void
stage_const_field2(rlk_wg_ctx_t *ctx, const void *data) {
    ctx->field[2] = *(const float *)data;
}

/* Derive field[2] as the average of field[0] and field[1]. */
static void
stage_derive_avg(rlk_wg_ctx_t *ctx, const void *data) {
    (void)data;
    ctx->field[2] = (ctx->field[0] + ctx->field[1]) * 0.5f;
}

/* Derive field[1] as 1 - field[0] (into a derived slot). */
static void
stage_derive_complement(rlk_wg_ctx_t *ctx, const void *data) {
    (void)data;
    ctx->field[1] = 1.0f - ctx->field[0];
}

/* No-op stage: only used to declare slot reads/writes in init tests. */
static void
stage_noop(rlk_wg_ctx_t *ctx, const void *data) {
    (void)ctx; (void)data;
}

/* Derive field[1] (wg_min: nsampled=1, so derived slot 0) as 2 * field[0]. */
static void
stage_derive_double(rlk_wg_ctx_t *ctx, const void *data) {
    (void)data;
    ctx->field[1] = 2.0f * ctx->field[0];
}

/* Sink capture for rlk_wg_run tests. */
static int g_run_x[256], g_run_y[256], g_run_wx[256], g_run_wy[256];

static void
sink_capture(const rlk_wg_ctx_t *ctx, void *user) {
    int *count = (int *)user;
    assert(*count < 256);
    g_run_x[*count] = ctx->x;
    g_run_y[*count] = ctx->y;
    g_run_wx[*count] = ctx->wx;
    g_run_wy[*count] = ctx->wy;
    (*count)++;
}

/* Config fixtures --------------------------------------------------------------- */

/* Minimal valid config: 1 FBM field, 1 full-coverage zone. */
static rlk_wg_t
wg_min(void) {
    rlk_wg_t wg = {0};
    wg.seed = 12345ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 1;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
    wg.naxes = 2;
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f, 0.0f}, .hi = {1.0f, 1.0f},
                                        .write = write_simple };
    return wg;
}

/* 2 fields mapped on 2 axes, 4 quadrant zones split at 0.5. */
static rlk_wg_t
wg_quad(float freq) {
    rlk_wg_t wg = {0};
    wg.seed = 12345ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 2;
    wg.fields[0] = (rlk_wg_field_t){ .freq = freq };
    wg.fields[1] = (rlk_wg_field_t){ .freq = freq };
    wg.naxes = 2;
    wg.axes[0] = (rlk_wg_axis_t){ .field = 0 };
    wg.axes[1] = (rlk_wg_axis_t){ .field = 1 };
    wg.nzones = 4;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f, 0.0f}, .hi = {0.5f, 0.5f}, .write = write_simple };
    wg.zones[1] = (rlk_wg_zone_t){ .lo = {0.5f, 0.0f}, .hi = {1.0f, 0.5f}, .write = write_simple };
    wg.zones[2] = (rlk_wg_zone_t){ .lo = {0.0f, 0.5f}, .hi = {0.5f, 1.0f}, .write = write_simple };
    wg.zones[3] = (rlk_wg_zone_t){ .lo = {0.5f, 0.5f}, .hi = {1.0f, 1.0f}, .write = write_simple };
    return wg;
}

/* Tilemap helper ----------------------------------------------------------------- */

/* 1 if at least one tile byte differs between a and b (same dimensions). */
static int
tm_differ(const rlk_tilemap_t *a, const rlk_tilemap_t *b) {
    for (int y = 0; y < a->h; y++) {
        for (int x = 0; x < a->w; x++) {
            const rlk_tile_t *t1 = rlk_tilemap_get(a, x, y);
            const rlk_tile_t *t2 = rlk_tilemap_get(b, x, y);
            if (t1->type != t2->type || t1->flags != t2->flags ||
                t1->biome != t2->biome || t1->variant != t2->variant)
                return 1;
        }
    }
    return 0;
}

/* Tests --------------------------------------------------------------------------- */

static void
test_rlk_wg_init_defaults(void) {
    rlk_wg_t wg = wg_min();
    assert(rlk_wg_init(&wg) == RLK_OK);
    assert(wg.nsampled == 1);
    assert(wg.fields[0].octaves == 3);        /* 0 -> 3 */
    assert(wg.fields[0].gain == 0.5f);       /* 0 -> 0.5 */
    assert(wg.nzones == 1);
    assert(wg.naxes == 2);                   /* set by fixture */
    assert(wg.depth_scale == 0.20f);         /* set by fixture, not default */
    assert(wg.transition_width == 0.20f);    /* set by fixture, not default */
    assert(wg.nstages == 0);
    assert(wg.nderived == 0);
}

static void
test_chunk_generation_minimal(void) {
    rlk_wg_t wg = wg_min();
    assert(rlk_wg_init(&wg) == RLK_OK);
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, 16, 16, tilebuf1, sizeof(tilebuf1));
    rlk_wg_chunk(&wg, 0, 0, &tm);
    /* At least one tile must have been written (biome set to zone 0). */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 0, 0);
    assert(t);
    assert(t->biome == 0);
}

static void
test_chunk_generation_deterministic(void) {
    rlk_wg_t wg1 = wg_quad(0.05f);
    rlk_wg_t wg2 = wg_quad(0.05f);
    assert(rlk_wg_init(&wg1) == RLK_OK);
    assert(rlk_wg_init(&wg2) == RLK_OK);
    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg1, 0, 0, &tm1);
    rlk_wg_chunk(&wg2, 0, 0, &tm2);
    assert(!tm_differ(&tm1, &tm2));
}

/* chunk(cx, cy) must equal fill() at the same world origin (incl. negative). */
static void
test_chunk_fill_equivalence(void) {
    rlk_wg_t wg = wg_quad(0.05f);
    assert(rlk_wg_init(&wg) == RLK_OK);
    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, 16, 16, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, 16, 16, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg, 2, -3, &tm1);
    rlk_wg_fill(&wg, 2 * 16, -3 * 16, &tm2);
    assert(!tm_differ(&tm1, &tm2));
}

static void
test_chunk_different_seed(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();
    wg2.seed = 54321ULL;
    wg1.zones[0].write = write_field0;
    wg2.zones[0].write = write_field0;
    assert(rlk_wg_init(&wg1) == RLK_OK);
    assert(rlk_wg_init(&wg2) == RLK_OK);
    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg1, 0, 0, &tm1);
    rlk_wg_chunk(&wg2, 0, 0, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* Deterministic classification: a stage pins field[0], so depth/edge/zone2
   are exact functions of the pinned value. Zones split axis 0 at 0.5,
   depth_scale = transition_width = 0.20. */
static void
test_classify_depth_edge(void) {
    const float cs[4] = { 0.25f, 0.45f, 0.50f, 0.75f };

    for (int i = 0; i < 4; i++) {
        rlk_wg_t wg = {0};
        wg.seed = 12345ULL;
        wg.depth_scale = 0.20f;
        wg.transition_width = 0.20f;
        wg.nsampled = 1;
        wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
        wg.naxes = 2;
        wg.nzones = 2;
        wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f, -RLK_WG_OPEN},
                                           .hi = {0.5f, RLK_WG_OPEN},
                                           .write = write_capture };
        wg.zones[1] = (rlk_wg_zone_t){ .lo = {0.5f, -RLK_WG_OPEN},
                                           .hi = {1.0f, RLK_WG_OPEN},
                                           .write = write_capture };
        assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_const_field0, .data = &cs[i],
                                                  .writes = { .sampled = RLK_WG_SLOT(0) } }) == RLK_OK);
        assert(rlk_wg_init(&wg) == RLK_OK);

        g_ctx_count = 0;
        rlk_tile_t buf;
        rlk_tilemap_t tm;
        rlk_tilemap_init(&tm, 1, 1, &buf, sizeof(buf));
        rlk_wg_fill(&wg, 0, 0, &tm);
        const rlk_wg_ctx_t *ctx = &g_ctx;

        /* Runner-up is always the other zone. */
        assert(ctx->zone2 == 1 - ctx->zone);

        switch (i) {
        case 0: /* deep inside zone 0 */
            assert(ctx->zone == 0);
            assert(ctx->depth > 0.99f);
            assert(ctx->edge == 0.0f);
            break;
        case 1: /* margin 0.05: depth = 0.25, edge = 0.5 */
            assert(ctx->zone == 0);
            assert(ctx->depth > 0.24f && ctx->depth < 0.26f);
            assert(ctx->edge > 0.49f && ctx->edge < 0.51f);
            break;
        case 2: /* exact tie: first zone wins, full edge */
            assert(ctx->zone == 0);
            assert(ctx->depth == 0.0f);
            assert(ctx->edge == 1.0f);
            break;
        case 3: /* deep inside zone 1 */
            assert(ctx->zone == 1);
            assert(ctx->depth > 0.99f);
            assert(ctx->edge == 0.0f);
            break;
        }
    }
}

/* Single zone: zone2 falls back to zone, edge is always 0. */
static void
test_single_zone_edge_zero(void) {
    rlk_wg_t wg = wg_min();
    float c = 0.5f;
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_const_field0, .data = &c,
                                              .writes = { .sampled = RLK_WG_SLOT(0) } }) == RLK_OK);
    wg.zones[0].write = write_capture;
    assert(rlk_wg_init(&wg) == RLK_OK);

    g_ctx_count = 0;
    rlk_tile_t buf;
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, 1, 1, &buf, sizeof(buf));
    rlk_wg_fill(&wg, 0, 0, &tm);
    const rlk_wg_ctx_t *ctx = &g_ctx;
    assert(ctx->zone == 0);
    assert(ctx->zone2 == 0);
    assert(ctx->edge == 0.0f);
    assert(ctx->depth > 0.99f);
}

/* 3 axes: zones split on axis 2 only. */
static void
test_zone3_classification(void) {
    const float cs[2] = { 0.25f, 0.75f };
    const uint8_t want[2] = { 0, 1 };

    for (int i = 0; i < 2; i++) {
        rlk_wg_t wg = {0};
        wg.seed = 12345ULL;
        wg.depth_scale = 0.20f;
        wg.transition_width = 0.20f;
        wg.nsampled = 3;
        for (int f = 0; f < 3; f++)
            wg.fields[f] = (rlk_wg_field_t){ .freq = 0.01f };
        wg.naxes = 3;
        wg.axes[0] = (rlk_wg_axis_t){ .field = 0 };
        wg.axes[1] = (rlk_wg_axis_t){ .field = 1 };
        wg.axes[2] = (rlk_wg_axis_t){ .field = 2 };
        wg.nzones = 2;
        wg.zones[0] = (rlk_wg_zone_t){ .lo = {-RLK_WG_OPEN, -RLK_WG_OPEN, 0.0f},
                                           .hi = { RLK_WG_OPEN,  RLK_WG_OPEN, 0.5f},
                                           .write = write_capture };
        wg.zones[1] = (rlk_wg_zone_t){ .lo = {-RLK_WG_OPEN, -RLK_WG_OPEN, 0.5f},
                                           .hi = { RLK_WG_OPEN,  RLK_WG_OPEN, 1.0f},
                                           .write = write_capture };
        assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_const_field2, .data = &cs[i],
                                                  .writes = { .sampled = RLK_WG_SLOT(2) } }) == RLK_OK);
        assert(rlk_wg_init(&wg) == RLK_OK);

        g_ctx_count = 0;
        rlk_tile_t buf;
        rlk_tilemap_t tm;
        rlk_tilemap_init(&tm, 1, 1, &buf, sizeof(buf));
        rlk_wg_fill(&wg, 0, 0, &tm);
        assert(g_ctx.zone == want[i]);
    }
}

/* rlk_wg_write_const copies type/flags/variant; fill sets biome = zone. */
static void
test_rlk_wg_write_const(void) {
    static const rlk_tile_t k_tile = { .type = 7, .flags = 3, .variant = 9 };
    rlk_wg_t wg = wg_min();
    wg.zones[0].write = rlk_wg_write_const;
    wg.zones[0].data = &k_tile;
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, 8, 8, tilebuf1, sizeof(tilebuf1));
    rlk_wg_fill(&wg, 100, 100, &tm);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            assert(t->type == 7);
            assert(t->flags == 3);
            assert(t->variant == 9);
            assert(t->biome == 0);
        }
    }
}

/* Ridge kind must produce a different field than FBM for the same seed. */
static void
test_chunk_ridge_field(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();
    wg2.fields[0].kind = RLK_WG_RIDGE;
    wg1.zones[0].write = write_field0;
    wg2.zones[0].write = write_field0;
    assert(rlk_wg_init(&wg1) == RLK_OK);
    assert(rlk_wg_init(&wg2) == RLK_OK);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg1, 0, 0, &tm1);
    rlk_wg_chunk(&wg2, 0, 0, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* Axis jitter must wobble the zone border somewhere on the map. */
static void
test_axis_jitter(void) {
    rlk_wg_t wg1 = wg_quad(0.05f);
    rlk_wg_t wg2 = wg_quad(0.05f);
    wg2.axes[0].jitter_field = 1;
    wg2.axes[0].jitter_amp = 0.04f;
    assert(rlk_wg_init(&wg1) == RLK_OK);
    assert(rlk_wg_init(&wg2) == RLK_OK);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg1, 1, 1, &tm1);
    rlk_wg_chunk(&wg2, 1, 1, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* Stage writes derived slot field[2] = avg(field[0], field[1]); the writer
   exposes all three, so the average identity is checked per tile. */
static void
test_stages_derive(void) {
    rlk_wg_t wg = wg_min();
    wg.nsampled = 2;
    wg.fields[1] = (rlk_wg_field_t){ .freq = 0.02f };
    wg.nderived = 1;
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_derive_avg, .data = NULL,
                                              .reads = { .sampled = RLK_WG_SLOT(0) | RLK_WG_SLOT(1) },
                                              .writes = { .derived = RLK_WG_SLOT(0) } }) == RLK_OK);
    wg.zones[0].write = write_fields012;
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_wg_chunk(&wg, 0, 0, &tm);
    for (int y = 0; y < MAP; y++) {
        for (int x = 0; x < MAP; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            int avg2 = 2 * (int)t->flags;
            int sum = (int)t->type + (int)t->variant;
            assert(avg2 - sum <= 3 && sum - avg2 <= 3);
        }
    }
}

/* Axis reading a derived slot: zone split on axis 1 = 1 - field[0]. */
static void
test_derived_slot_axis(void) {
    rlk_wg_t wg = {0};
    wg.seed = 12345ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 1;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.05f };
    wg.nderived = 1;
    wg.naxes = 2;
    wg.axes[0] = (rlk_wg_axis_t){ .field = 0 };
    wg.axes[1] = (rlk_wg_axis_t){ .field = 1 };
    wg.nzones = 2;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {-RLK_WG_OPEN, 0.0f},
                                       .hi = { RLK_WG_OPEN, 0.5f},
                                       .write = write_field0 };
    wg.zones[1] = (rlk_wg_zone_t){ .lo = {-RLK_WG_OPEN, 0.5f},
                                       .hi = { RLK_WG_OPEN, 1.0f},
                                       .write = write_field0 };
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_derive_complement, .data = NULL,
                                              .reads = { .sampled = RLK_WG_SLOT(0) },
                                              .writes = { .derived = RLK_WG_SLOT(0) } }) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_wg_chunk(&wg, 2, 1, &tm);
    for (int y = 0; y < MAP; y++) {
        for (int x = 0; x < MAP; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            /* variant = trunc(field[0]*255): skip the ambiguous quantization
               step around 0.5, then biome must follow the derived value. */
            if (t->variant == 127)
                continue;
            assert(t->biome == (t->variant > 127 ? 0 : 1));
        }
    }
}

static void
test_rlk_wg_rand_basic(void) {
    rlk_wg_t wg = wg_min();
    assert(rlk_wg_init(&wg) == RLK_OK);
    float v = rlk_wg_rand(&wg, 10, 20, 0);
    assert(v >= 0.0f && v < 1.0f);
}

static void
test_rlk_wg_rand_deterministic(void) {
    rlk_wg_t wg = wg_min();
    assert(rlk_wg_init(&wg) == RLK_OK);
    assert(rlk_wg_rand(&wg, 10, 20, 0) == rlk_wg_rand(&wg, 10, 20, 0));
}

static void
test_rlk_wg_rand_different_salt(void) {
    rlk_wg_t wg = wg_min();
    assert(rlk_wg_init(&wg) == RLK_OK);
    assert(rlk_wg_rand(&wg, 10, 20, 0) != rlk_wg_rand(&wg, 10, 20, 1));
}

static void
test_zone_classification_2d(void) {
    rlk_wg_t wg = wg_quad(0.01f);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_wg_chunk(&wg, 0, 0, &tm);

    int zone_count[4] = {0};
    for (int y = 0; y < MAP; y++) {
        for (int x = 0; x < MAP; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            assert(t->biome < 4);
            zone_count[t->biome]++;
        }
    }
    for (int i = 0; i < 4; i++)
        assert(zone_count[i] > 0);
}

/* rlk_wg_add appends stages and rejects overflow. */
static void
test_rlk_wg_add(void) {
    rlk_wg_t wg = wg_min();
    float c = 0.5f;
    rlk_wg_stage_t st = { .fn = stage_const_field0, .data = &c };

    assert(rlk_wg_add(&wg, st) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].fn == st.fn);
    assert(wg.stages[0].data == &c);

    /* Fill remaining slots. */
    for (int i = 1; i < RLK_WG_MAX_STAGES; i++)
        assert(rlk_wg_add(&wg, st) == RLK_OK);
    assert(wg.nstages == RLK_WG_MAX_STAGES);

    /* Table full. */
    assert(rlk_wg_add(&wg, st) == RLK_E_INVALID);
    assert(wg.nstages == RLK_WG_MAX_STAGES);
}

/* init accepts a stage reading a derived slot written by an earlier stage. */
static void
test_init_accepts_ordered_slots(void) {
    rlk_wg_t wg = wg_min();
    wg.nderived = 1;
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_noop, .data = NULL,
                                              .writes = { .derived = RLK_WG_SLOT(0) } }) == RLK_OK);
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_noop, .data = NULL,
                                              .reads = { .derived = RLK_WG_SLOT(0) } }) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);
}

/* init rejects a stage reading a derived slot that no earlier stage writes. */
static void
test_init_rejects_unwritten_read(void) {
    rlk_wg_t wg = wg_min();
    wg.nderived = 1;
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_noop, .data = NULL,
                                              .reads = { .derived = RLK_WG_SLOT(0) } }) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_E_INVALID);
}

/* init rejects the same read when the writer is registered later. */
static void
test_init_rejects_wrong_order(void) {
    rlk_wg_t wg = wg_min();
    wg.nderived = 1;
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_noop, .data = NULL,
                                              .reads = { .derived = RLK_WG_SLOT(0) } }) == RLK_OK);
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_noop, .data = NULL,
                                              .writes = { .derived = RLK_WG_SLOT(0) } }) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_E_INVALID);
}

/* init rejects a slot reference out of range. */
static void
test_init_rejects_out_of_range(void) {
    rlk_wg_t wg = wg_min();   /* nsampled = 1, nderived = 0 */
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_noop, .data = NULL,
                                              .reads = { .sampled = RLK_WG_SLOT(5) } }) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_E_INVALID);
}

/* init rejects an axis reading a derived slot that no stage writes. */
static void
test_init_rejects_axis_on_unwritten_slot(void) {
    rlk_wg_t wg = wg_min();
    wg.nderived = 1;
    wg.axes[1] = (rlk_wg_axis_t){ .field = 1 };   /* derived slot 0, never written */
    assert(rlk_wg_init(&wg) == RLK_E_INVALID);
}

/* RLK_WG_SLOTS_ALL expands to the existing slots only. */
static void
test_init_accepts_slots_all(void) {
    rlk_wg_t wg = wg_min();
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_noop, .data = NULL,
                                              .reads = { .sampled = RLK_WG_SLOTS_ALL },
                                              .writes = { .sampled = RLK_WG_SLOTS_ALL } }) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);
}

/* rlk_wg_run calls the sink once per tile, row-major, with world coords
   offset by the run origin and local coords reset to the run. */
static void
test_rlk_wg_run_sink_order(void) {
    rlk_wg_t wg = wg_min();
    assert(rlk_wg_init(&wg) == RLK_OK);
    int count = 0;
    rlk_wg_run(&wg, 100, -50, 4, 3, sink_capture, &count);
    assert(count == 12);
    for (int k = 0; k < count; k++) {
        int x = k % 4, y = k / 4;
        assert(g_run_x[k] == x && g_run_y[k] == y);
        assert(g_run_wx[k] == 100 + x && g_run_wy[k] == -50 + y);
    }
}

/* rlk_wg_fill_slot on a sampled slot: each cell equals rlk_wg_sample_field
   at the cell centre (cellsize > 1, negative origin). */
static void
test_rlk_wg_fill_slot_sampled(void) {
    rlk_wg_t wg = wg_min();
    assert(rlk_wg_init(&wg) == RLK_OK);

    static float buf[4 * 3];
    rlk_grid_t g = { .lattice = { .w = 4, .h = 3, .cellsize = 8,
                                  .origin_x = -16, .origin_y = 24 }, .v = buf };
    rlk_wg_fill_slot(&wg, 0, &g);
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 4; i++) {
            float cx, cy;
            rlk_grid_center(&g, i, j, &cx, &cy);
            float want = rlk_wg_sample_field(&wg, 0, cx, cy);
            float got = buf[(size_t)j * 4 + (size_t)i];
            assert(fabsf(got - want) < 1e-6f);
        }
}

/* rlk_wg_fill_slot on a derived slot reflects the stage output; the pipeline
   runs at the cell centre (cellsize = 1 -> tile centre x+0.5). */
static void
test_rlk_wg_fill_slot_derived(void) {
    rlk_wg_t wg = wg_min();
    wg.nderived = 1;
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = stage_derive_double, .data = NULL,
                                              .reads = { .sampled = RLK_WG_SLOT(0) },
                                              .writes = { .derived = RLK_WG_SLOT(0) } }) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    static float buf[3 * 2];
    rlk_grid_t g = { .lattice = { .w = 3, .h = 2, .cellsize = 1,
                                  .origin_x = 5, .origin_y = 7 }, .v = buf };
    rlk_wg_fill_slot(&wg, 1, &g);   /* slot 1 = nsampled(1) + derived 0 */
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 3; i++) {
            float cx, cy;
            rlk_grid_center(&g, i, j, &cx, &cy);
            float want = 2.0f * rlk_wg_sample_field(&wg, 0, cx, cy);
            assert(fabsf(buf[(size_t)j * 3 + (size_t)i] - want) < 1e-6f);
        }
}

int
main(void) {
    test_rlk_wg_init_defaults();
    test_chunk_generation_minimal();
    test_chunk_generation_deterministic();
    test_chunk_fill_equivalence();
    test_chunk_different_seed();
    test_classify_depth_edge();
    test_single_zone_edge_zero();
    test_zone3_classification();
    test_rlk_wg_write_const();
    test_chunk_ridge_field();
    test_axis_jitter();
    test_stages_derive();
    test_derived_slot_axis();
    test_rlk_wg_rand_basic();
    test_rlk_wg_rand_deterministic();
    test_rlk_wg_rand_different_salt();
    test_zone_classification_2d();
    test_rlk_wg_add();
    test_init_accepts_ordered_slots();
    test_init_rejects_unwritten_read();
    test_init_rejects_wrong_order();
    test_init_rejects_out_of_range();
    test_init_rejects_axis_on_unwritten_slot();
    test_init_accepts_slots_all();
    test_rlk_wg_run_sink_order();
    test_rlk_wg_fill_slot_sampled();
    test_rlk_wg_fill_slot_derived();
    return 0;
}
