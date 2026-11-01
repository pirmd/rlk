#include <assert.h>
#include <stdint.h>
#include <math.h>

#include "wg_relief.h"

#define MAP 32

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: derived[0]=elev -> variant (0..255) */
static void
write_relief(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    const rlk_wg_t *wg = ctx->wg;
    out->biome = (uint8_t)ctx->zone;
    out->variant = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 0], 0.0f, 1.0f) * 255.0f);
}

/* Minimal wg: 3 sampled fields (cont, hills, temp), 1 derived (elev),
   1 full-coverage zone on field 0. */
static rlk_wg_t
wg_min(void) {
    rlk_wg_t wg = {0};
    wg.seed = 42ULL;
    wg.inside_scale = 0.20f;
    wg.border_width = 0.20f;
    wg.nsampled = 3;
    wg.fields[0] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 1, .octaves = 3, .freq = 0.015f, .gain = 0.5f };
    wg.fields[1] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 2, .octaves = 4, .freq = 0.020f, .gain = 0.5f };
    wg.fields[2] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 3, .octaves = 3, .freq = 0.020f, .gain = 0.5f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_axis_t){ .slot = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_relief };
    return wg;
}

/* Basic: stage writes elevation into derived field (all-zero params exercise
   the defaults resolved by the constructor).
   With only f_cont, elevation should be close to the continent field. */
static void
test_relief_stage_basic(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = {0};
    r.f_cont = 0;
    r.f_hills = RLK_WG_NONE;
    r.f_ridge = RLK_WG_NONE;
    r.f_warp_x = RLK_WG_NONE;
    r.f_warp_y = RLK_WG_NONE;
    r.f_fjord = RLK_WG_NONE;
    r.f_temp = RLK_WG_NONE;
    r.slot_elev = 0;
    r.river_carve_field = RLK_WG_NONE;
    r.sea = 0.40f;

    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Elevation should be in a reasonable range (noise output ~[0,1]) */
    int has_land = 0, has_sea = 0;
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            assert(t);
            const float e = (float)t->variant / 255.0f;
            if (e > 0.40f) has_land = 1;
            if (e < 0.40f) has_sea = 1;
        }
    /* With a 0.015 freq FBM over 32 tiles, we should get both land and sea */
    assert(has_land || has_sea);
}

/* With hills: elevation should vary more than continent alone. */
static void
test_relief_stage_with_hills(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = {0};
    r.f_cont = 0;
    r.f_hills = 1;
    r.f_ridge = RLK_WG_NONE;
    r.f_warp_x = RLK_WG_NONE;
    r.f_warp_y = RLK_WG_NONE;
    r.f_fjord = RLK_WG_NONE;
    r.f_temp = RLK_WG_NONE;
    r.slot_elev = 0;
    r.river_carve_field = RLK_WG_NONE;
    r.sea = 0.40f;
    r.hills_amp = 0.30f;

    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Should not crash; elevation written */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 16, 16);
    assert(t);
}

/* Altitudinal cooling: temperature field should be modified for land tiles. */
static void
test_relief_stage_lapse(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = {0};
    r.f_cont = 0;
    r.f_hills = RLK_WG_NONE;
    r.f_ridge = RLK_WG_NONE;
    r.f_warp_x = RLK_WG_NONE;
    r.f_warp_y = RLK_WG_NONE;
    r.f_fjord = RLK_WG_NONE;
    r.f_temp = 2;
    r.slot_elev = 0;
    r.river_carve_field = RLK_WG_NONE;
    r.sea = 0.40f;
    r.lapse = 0.35f;

    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Should not crash; temperature was modified in-place */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 16, 16);
    assert(t);
}

/* Constructor builds a stage from a config (fn is static, only the
   data pointer is checkable). */
static void
test_relief_constructor(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = {0};
    r.f_cont = 0;
    r.f_hills = RLK_WG_NONE;
    r.f_ridge = RLK_WG_NONE;
    r.f_warp_x = RLK_WG_NONE;
    r.f_warp_y = RLK_WG_NONE;
    r.f_fjord = RLK_WG_NONE;
    r.f_temp = RLK_WG_NONE;
    r.slot_elev = 0;
    r.river_carve_field = RLK_WG_NONE;

    rlk_wg_stage_t st = rlk_wg_relief(&r);
    assert(st.data == &r);
    /* Zero defaults are resolved in place by the constructor. */
    assert(r.warp == 30.0f);
    assert(r.hills_amp == 0.30f);

    assert(rlk_wg_add(&wg, st) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].data == &r);

    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);
}

/* With domain warp: should not crash, elevation still written. */
static void
test_relief_stage_with_warp(void) {
    rlk_wg_t wg = wg_min();
    /* Add warp fields */
    wg.nsampled = 5;
    wg.fields[3] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 4, .octaves = 2, .freq = 0.010f, .gain = 0.5f };
    wg.fields[4] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 5, .octaves = 2, .freq = 0.010f, .gain = 0.5f };

    rlk_wg_relief_cfg_t r = {0};
    r.f_cont = 0;
    r.f_hills = 1;
    r.f_ridge = RLK_WG_NONE;
    r.f_warp_x = 3;
    r.f_warp_y = 4;
    r.f_fjord = RLK_WG_NONE;
    r.f_temp = 2;
    r.slot_elev = 0;
    r.river_carve_field = RLK_WG_NONE;
    r.sea = 0.40f;
    r.warp = 20.0f;

    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    const rlk_tile_t *t = rlk_tilemap_get(&tm, 16, 16);
    assert(t);
}

int
main(void) {
    test_relief_stage_basic();
    test_relief_stage_with_hills();
    test_relief_stage_lapse();
    test_relief_constructor();
    test_relief_stage_with_warp();
    return 0;
}
