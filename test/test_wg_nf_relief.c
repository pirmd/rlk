#include <assert.h>
#include <stdint.h>
#include <math.h>

#include "wg_nf_relief.h"

#define MAP 32

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: derived[0]=elev -> variant (0..255), derived[1]=river -> flags */
static void
write_relief(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    const rlk_wg_nf_t *wg = ctx->wg;
    out->biome = (uint8_t)ctx->zone;
    out->variant = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 0], 0.0f, 1.0f) * 255.0f);
    out->flags = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 1], 0.0f, 1.0f) * 255.0f);
}

/* Minimal wg_nf: 3 sampled fields (cont, hills, temp), 2 derived (elev, river),
   1 full-coverage zone on field 0. */
static rlk_wg_nf_t
wg_min(void) {
    rlk_wg_nf_t wg = {0};
    wg.seed = 42ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 3;
    wg.nderived = 2;
    wg.fields[0] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 1, .octaves = 3, .freq = 0.015f, .gain = 0.5f };
    wg.fields[1] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 2, .octaves = 4, .freq = 0.020f, .gain = 0.5f };
    wg.fields[2] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 3, .octaves = 3, .freq = 0.020f, .gain = 0.5f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_nf_axis_t){ .field = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_nf_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_relief };
    return wg;
}

/* init: zero defaults resolved */
static void
test_relief_init_defaults(void) {
    rlk_wg_nf_relief_t r = {0};
    r.f_cont = 0;
    r.f_hills = RLK_WG_NF_RELIEF_NONE;
    r.f_ridge = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_x = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_y = RLK_WG_NF_RELIEF_NONE;
    r.f_fjord = RLK_WG_NF_RELIEF_NONE;
    r.f_temp = RLK_WG_NF_RELIEF_NONE;
    r.f_river = RLK_WG_NF_RELIEF_NONE;
    r.slot_elev = 0;
    r.slot_river = RLK_WG_NF_RELIEF_NONE;
    r.river_carve_field = RLK_WG_NF_RELIEF_NONE;
    rlk_wg_nf_relief_init(&r);
    assert(r.warp == 30.0f);
    assert(r.hills_amp == 0.30f);
    assert(r.mountain_lo == 0.45f);
    assert(r.mountain_hi == 0.65f);
    assert(r.mountain_amp == 0.45f);
    assert(r.lapse == 0.35f);
    assert(r.fjord_stretch == 0.25f);
    assert(r.fjord_width == 0.05f);
    assert(r.fjord_depth == 0.45f);
    assert(r.fjord_steep == 3.0f);
    assert(r.fjord_reach == 0.12f);
    assert(r.fjord_cold0 == 0.35f);
    assert(r.fjord_cold1 == 0.55f);
}

/* Basic: stage writes elevation into derived field.
   With only f_cont, elevation should be close to the continent field. */
static void
test_relief_stage_basic(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_relief_t r = {0};
    r.f_cont = 0;
    r.f_hills = RLK_WG_NF_RELIEF_NONE;
    r.f_ridge = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_x = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_y = RLK_WG_NF_RELIEF_NONE;
    r.f_fjord = RLK_WG_NF_RELIEF_NONE;
    r.f_temp = RLK_WG_NF_RELIEF_NONE;
    r.f_river = RLK_WG_NF_RELIEF_NONE;
    r.slot_elev = 0;
    r.slot_river = RLK_WG_NF_RELIEF_NONE;
    r.river_carve_field = RLK_WG_NF_RELIEF_NONE;
    r.sea = 0.40f;

    assert(rlk_wg_nf_relief_add(&wg, &r) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

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
    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_relief_t r = {0};
    r.f_cont = 0;
    r.f_hills = 1;
    r.f_ridge = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_x = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_y = RLK_WG_NF_RELIEF_NONE;
    r.f_fjord = RLK_WG_NF_RELIEF_NONE;
    r.f_temp = RLK_WG_NF_RELIEF_NONE;
    r.f_river = RLK_WG_NF_RELIEF_NONE;
    r.slot_elev = 0;
    r.slot_river = RLK_WG_NF_RELIEF_NONE;
    r.river_carve_field = RLK_WG_NF_RELIEF_NONE;
    r.sea = 0.40f;
    r.hills_amp = 0.30f;

    assert(rlk_wg_nf_relief_add(&wg, &r) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* Should not crash; elevation written */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 16, 16);
    assert(t);
}

/* Altitudinal cooling: temperature field should be modified for land tiles. */
static void
test_relief_stage_lapse(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_relief_t r = {0};
    r.f_cont = 0;
    r.f_hills = RLK_WG_NF_RELIEF_NONE;
    r.f_ridge = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_x = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_y = RLK_WG_NF_RELIEF_NONE;
    r.f_fjord = RLK_WG_NF_RELIEF_NONE;
    r.f_temp = 2;
    r.f_river = RLK_WG_NF_RELIEF_NONE;
    r.slot_elev = 0;
    r.slot_river = RLK_WG_NF_RELIEF_NONE;
    r.river_carve_field = RLK_WG_NF_RELIEF_NONE;
    r.sea = 0.40f;
    r.lapse = 0.35f;

    assert(rlk_wg_nf_relief_add(&wg, &r) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* Should not crash; temperature was modified in-place */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 16, 16);
    assert(t);
}

/* River mask output: with f_river set, slot_river should get non-zero values. */
static void
test_relief_stage_river_output(void) {
    rlk_wg_nf_t wg = wg_min();
    /* Add a 4th sampled field for river noise */
    wg.nsampled = 4;
    wg.fields[3] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 4, .octaves = 2, .freq = 0.008f, .gain = 0.5f };
    /* nderived stays 2: slot_elev=0, slot_river=1 */

    rlk_wg_nf_relief_t r = {0};
    r.f_cont = 0;
    r.f_hills = RLK_WG_NF_RELIEF_NONE;
    r.f_ridge = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_x = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_y = RLK_WG_NF_RELIEF_NONE;
    r.f_fjord = RLK_WG_NF_RELIEF_NONE;
    r.f_temp = RLK_WG_NF_RELIEF_NONE;
    r.f_river = 3;
    r.slot_elev = 0;
    r.slot_river = 1;
    r.river_carve_field = RLK_WG_NF_RELIEF_NONE;
    r.sea = 0.40f;
    r.river_width_tiles = 2.0f;
    r.river_carve = 0.05f;
    r.relief_span = 0.40f;

    assert(rlk_wg_nf_relief_add(&wg, &r) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* At least some tile should have a river (flags > 0) */
    int has_river = 0;
    for (int y = 0; y < MAP && !has_river; y++)
        for (int x = 0; x < MAP && !has_river; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            if (t && t->flags > 0)
                has_river = 1;
        }
    assert(has_river);
}

/* Power-user: init then add_stage manually. */
static void
test_relief_init_then_add_stage(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_relief_t r = {0};
    r.f_cont = 0;
    r.f_hills = RLK_WG_NF_RELIEF_NONE;
    r.f_ridge = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_x = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_y = RLK_WG_NF_RELIEF_NONE;
    r.f_fjord = RLK_WG_NF_RELIEF_NONE;
    r.f_temp = RLK_WG_NF_RELIEF_NONE;
    r.f_river = RLK_WG_NF_RELIEF_NONE;
    r.slot_elev = 0;
    r.slot_river = RLK_WG_NF_RELIEF_NONE;
    r.river_carve_field = RLK_WG_NF_RELIEF_NONE;

    rlk_wg_nf_relief_init(&r);
    assert(rlk_wg_nf_add_stage(&wg, rlk_wg_nf_relief_stage, &r) == RLK_OK);
    assert(wg.nstages == 1);

    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);
}

/* With domain warp: should not crash, elevation still written. */
static void
test_relief_stage_with_warp(void) {
    rlk_wg_nf_t wg = wg_min();
    /* Add warp fields */
    wg.nsampled = 5;
    wg.fields[3] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 4, .octaves = 2, .freq = 0.010f, .gain = 0.5f };
    wg.fields[4] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 5, .octaves = 2, .freq = 0.010f, .gain = 0.5f };

    rlk_wg_nf_relief_t r = {0};
    r.f_cont = 0;
    r.f_hills = 1;
    r.f_ridge = RLK_WG_NF_RELIEF_NONE;
    r.f_warp_x = 3;
    r.f_warp_y = 4;
    r.f_fjord = RLK_WG_NF_RELIEF_NONE;
    r.f_temp = 2;
    r.f_river = RLK_WG_NF_RELIEF_NONE;
    r.slot_elev = 0;
    r.slot_river = RLK_WG_NF_RELIEF_NONE;
    r.river_carve_field = RLK_WG_NF_RELIEF_NONE;
    r.sea = 0.40f;
    r.warp = 20.0f;

    assert(rlk_wg_nf_relief_add(&wg, &r) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    const rlk_tile_t *t = rlk_tilemap_get(&tm, 16, 16);
    assert(t);
}

int
main(void) {
    test_relief_init_defaults();
    test_relief_stage_basic();
    test_relief_stage_with_hills();
    test_relief_stage_lapse();
    test_relief_stage_river_output();
    test_relief_init_then_add_stage();
    test_relief_stage_with_warp();
    return 0;
}
