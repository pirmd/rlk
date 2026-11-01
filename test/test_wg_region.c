#include <assert.h>
#include <stdint.h>
#include "wg_region.h"

#define MAP 64

static rlk_tile_t tilebuf1[MAP * MAP];
static rlk_tile_t tilebuf2[MAP * MAP];

/* Writers --------------------------------------------------------------------- */

/* Biome + clamped field[0] quantized into variant. */
static void
write_field0(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->biome = (uint8_t)ctx->zone;
    out->variant = (uint8_t)(rlk_clampf(ctx->field[0], 0.0f, 1.0f) * 255.0f);
}

/* Fixtures ---------------------------------------------------------------------- */

/* Minimal valid config: 1 FBM field, 1 full-coverage zone writing field[0]. */
static rlk_wg_t
wg_min(void) {
    rlk_wg_t wg = {0};
    wg.seed = 12345ULL;
    wg.inside_scale = 0.20f;
    wg.border_width = 0.20f;
    wg.nsampled = 1;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
    wg.naxes = 2;
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f, 0.0f}, .hi = {1.0f, 1.0f},
                                        .write = write_field0 };
    return wg;
}

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

/* Voronoi bias must shift the field, hence the written tiles. */
static void
test_region_basic(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_region_cfg_t mod = {0};
    mod.ntypes = 2;
    mod.cell = 64;
    mod.blend = 16;
    mod.types[0].bias[0] = +0.1f;
    mod.types[1].bias[0] = -0.1f;

    assert(rlk_wg_add(&wg2, rlk_wg_region(&mod)) == RLK_OK);
    rlk_wg_init(&wg1);
    rlk_wg_init(&wg2);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_fill(&wg1, 0, 0, &tm1);
    rlk_wg_fill(&wg2, 0, 0, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* Neutral region (bias = gain = 0) must be a no-op: field unchanged. */
static void
test_region_neutral_noop(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_region_cfg_t mod = {0};
    mod.ntypes = 1;
    mod.cell = 64;
    mod.blend = 16;

    assert(rlk_wg_add(&wg2, rlk_wg_region(&mod)) == RLK_OK);
    rlk_wg_init(&wg1);
    rlk_wg_init(&wg2);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_fill(&wg1, 0, 0, &tm1);
    rlk_wg_fill(&wg2, 0, 0, &tm2);
    assert(!tm_differ(&tm1, &tm2));
}

/* blend < 0 = hard borders (nearest cell wins, no blend): must differ from
   a blended border. */
static void
test_region_hard_border(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_region_cfg_t soft = {0};
    soft.ntypes = 2;
    soft.cell = 16;
    soft.blend = 8;
    soft.types[0].bias[0] = +0.1f;
    soft.types[1].bias[0] = -0.1f;

    rlk_wg_region_cfg_t hard = soft;
    hard.blend = -1;

    assert(rlk_wg_add(&wg1, rlk_wg_region(&soft)) == RLK_OK);
    assert(rlk_wg_add(&wg2, rlk_wg_region(&hard)) == RLK_OK);
    rlk_wg_init(&wg1);
    rlk_wg_init(&wg2);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_fill(&wg1, 0, 0, &tm1);
    rlk_wg_fill(&wg2, 0, 0, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* Zero cell/blend resolve to defaults (128 / cell/4) in the constructor;
   neutral stays a no-op. */
static void
test_region_defaults(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_region_cfg_t mod = {0};
    mod.ntypes = 1;

    assert(rlk_wg_add(&wg2, rlk_wg_region(&mod)) == RLK_OK);
    /* Zero defaults are resolved in place by the constructor. */
    assert(mod.cell == 128);
    assert(mod.blend == 32);
    rlk_wg_init(&wg1);
    rlk_wg_init(&wg2);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_fill(&wg1, 0, 0, &tm1);
    rlk_wg_fill(&wg2, 0, 0, &tm2);
    assert(!tm_differ(&tm1, &tm2));
}

/* Gain scales the field: output must differ from the unmodulated one. */
static void
test_region_with_gain(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_region_cfg_t mod = {0};
    mod.ntypes = 1;
    mod.cell = 64;
    mod.blend = 16;
    mod.types[0].gain[0] = 0.5f;  /* scale = 1 + 0.5 = 1.5 */

    assert(rlk_wg_add(&wg2, rlk_wg_region(&mod)) == RLK_OK);
    rlk_wg_init(&wg1);
    rlk_wg_init(&wg2);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_fill(&wg1, 0, 0, &tm1);
    rlk_wg_fill(&wg2, 0, 0, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* Same cfg on two wg: identical output (the stage is pure in seed/wx/wy). */
static void
test_region_deterministic(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_region_cfg_t mod = {0};
    mod.ntypes = 3;
    mod.cell = 32;
    mod.blend = 8;
    mod.types[1].bias[0] = +0.1f;
    mod.types[2].bias[0] = -0.1f;

    assert(rlk_wg_add(&wg1, rlk_wg_region(&mod)) == RLK_OK);
    assert(rlk_wg_add(&wg2, rlk_wg_region(&mod)) == RLK_OK);
    rlk_wg_init(&wg1);
    rlk_wg_init(&wg2);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_fill(&wg1, 0, 0, &tm1);
    rlk_wg_fill(&wg2, 0, 0, &tm2);
    assert(!tm_differ(&tm1, &tm2));
}

/* A different salt decorrelates the region layout. */
static void
test_region_salt(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_region_cfg_t mod = {0};
    mod.ntypes = 2;
    mod.cell = 16;
    mod.blend = 4;
    mod.salt = 1;
    mod.types[0].bias[0] = +0.1f;
    mod.types[1].bias[0] = -0.1f;

    rlk_wg_region_cfg_t mod2 = mod;
    mod2.salt = 2;

    assert(rlk_wg_add(&wg1, rlk_wg_region(&mod)) == RLK_OK);
    assert(rlk_wg_add(&wg2, rlk_wg_region(&mod2)) == RLK_OK);
    rlk_wg_init(&wg1);
    rlk_wg_init(&wg2);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_fill(&wg1, 0, 0, &tm1);
    rlk_wg_fill(&wg2, 0, 0, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* Constructor builds a stage from a config (fn is static, only the
   data pointer is checkable). */
static void
test_region_constructor(void) {
    rlk_wg_t wg = wg_min();

    rlk_wg_region_cfg_t mod = {0};
    mod.ntypes = 2;
    mod.types[0].bias[0] = +0.1f;
    mod.types[1].bias[0] = -0.1f;

    rlk_wg_stage_t st = rlk_wg_region(&mod);
    assert(st.data == &mod);
    /* Zero defaults are resolved in place by the constructor. */
    assert(mod.cell == 128);
    assert(mod.blend == 32);

    assert(rlk_wg_add(&wg, st) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].data == &mod);

    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_wg_fill(&wg, 0, 0, &tm);
}

int
main(void) {
    test_region_basic();
    test_region_neutral_noop();
    test_region_defaults();
    test_region_hard_border();
    test_region_with_gain();
    test_region_deterministic();
    test_region_salt();
    test_region_constructor();
    return 0;
}
