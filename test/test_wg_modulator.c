#include <assert.h>
#include <stdint.h>
#include "wg_modulator.h"

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

/* Fixtures ---------------------------------------------------------------------- */

/* Minimal valid config: 1 FBM field, 1 full-coverage zone writing field[0]. */
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
test_modulator_basic(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_modulator_cfg_t mod = {0};
    mod.ntypes = 2;
    mod.cell = 64.0f;
    mod.blend = 16.0f;
    mod.types[0].bias[0] = +0.1f;
    mod.types[1].bias[0] = -0.1f;

    assert(rlk_wg_add(&wg2, rlk_wg_modulator(&mod)) == RLK_OK);
    assert(rlk_wg_init(&wg1) == RLK_OK);
    assert(rlk_wg_init(&wg2) == RLK_OK);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg1, 0, 0, &tm1);
    rlk_wg_chunk(&wg2, 0, 0, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* Neutral modulator (bias = gain = 0) must be a no-op: field unchanged. */
static void
test_modulator_neutral_noop(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_modulator_cfg_t mod = {0};
    mod.ntypes = 1;
    mod.cell = 64.0f;
    mod.blend = 16.0f;

    assert(rlk_wg_add(&wg2, rlk_wg_modulator(&mod)) == RLK_OK);
    assert(rlk_wg_init(&wg1) == RLK_OK);
    assert(rlk_wg_init(&wg2) == RLK_OK);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg1, 0, 0, &tm1);
    rlk_wg_chunk(&wg2, 0, 0, &tm2);
    assert(!tm_differ(&tm1, &tm2));
}

/* Zero cell/blend resolve to defaults (128 / cell/4) inside the stage;
   neutral stays a no-op. */
static void
test_modulator_defaults(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_modulator_cfg_t mod = {0};
    mod.ntypes = 1;

    assert(rlk_wg_add(&wg2, rlk_wg_modulator(&mod)) == RLK_OK);
    /* cfg is const: zero defaults are resolved in the stage, not written back. */
    assert(mod.cell == 0.0f);
    assert(mod.blend == 0.0f);
    assert(rlk_wg_init(&wg1) == RLK_OK);
    assert(rlk_wg_init(&wg2) == RLK_OK);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg1, 0, 0, &tm1);
    rlk_wg_chunk(&wg2, 0, 0, &tm2);
    assert(!tm_differ(&tm1, &tm2));
}

/* Gain scales the field: output must differ from the unmodulated one. */
static void
test_modulator_with_gain(void) {
    rlk_wg_t wg1 = wg_min();
    rlk_wg_t wg2 = wg_min();

    rlk_wg_modulator_cfg_t mod = {0};
    mod.ntypes = 1;
    mod.cell = 64.0f;
    mod.blend = 16.0f;
    mod.types[0].gain[0] = 0.5f;  /* scale = 1 + 0.5 = 1.5 */

    assert(rlk_wg_add(&wg2, rlk_wg_modulator(&mod)) == RLK_OK);
    assert(rlk_wg_init(&wg1) == RLK_OK);
    assert(rlk_wg_init(&wg2) == RLK_OK);

    rlk_tilemap_t tm1, tm2;
    rlk_tilemap_init(&tm1, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_tilemap_init(&tm2, MAP, MAP, tilebuf2, sizeof(tilebuf2));
    rlk_wg_chunk(&wg1, 0, 0, &tm1);
    rlk_wg_chunk(&wg2, 0, 0, &tm2);
    assert(tm_differ(&tm1, &tm2));
}

/* System limit: stage slots exhausted -> RLK_E_INVALID. */
static void
test_modulator_limit_too_many_stages(void) {
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

    rlk_wg_modulator_cfg_t mod = {0};
    mod.ntypes = 1;
    mod.cell = 64.0f;
    for (int i = 0; i < RLK_WG_MAX_STAGES; i++)
        assert(rlk_wg_add(&wg, rlk_wg_modulator(&mod)) == RLK_OK);

    assert(rlk_wg_add(&wg, rlk_wg_modulator(&mod)) == RLK_E_INVALID);
}

/* Constructor builds a stage from a const config (fn is static, only the
   data pointer is checkable). */
static void
test_modulator_constructor(void) {
    rlk_wg_t wg = wg_min();

    rlk_wg_modulator_cfg_t mod = {0};
    mod.ntypes = 2;
    mod.types[0].bias[0] = +0.1f;
    mod.types[1].bias[0] = -0.1f;

    rlk_wg_stage_t st = rlk_wg_modulator(&mod);
    assert(st.data == &mod);
    /* cfg is const: zero defaults were NOT written back. */
    assert(mod.cell == 0.0f);
    assert(mod.blend == 0.0f);

    assert(rlk_wg_add(&wg, st) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].data == &mod);

    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf1, sizeof(tilebuf1));
    rlk_wg_chunk(&wg, 0, 0, &tm);
}

int
main(void) {
    test_modulator_basic();
    test_modulator_neutral_noop();
    test_modulator_defaults();
    test_modulator_with_gain();
    test_modulator_limit_too_many_stages();
    test_modulator_constructor();
    return 0;
}
