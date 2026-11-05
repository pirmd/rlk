#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "wg_relief.h"
#include "fmath.h"

#define MAP 32

static rlk_tile_t tilebuf[MAP * MAP];

/* Raw output buffers filled by the zone writer (run-local tile index). */
static float elev_out[MAP * MAP];
static float temp_out[MAP * MAP];

typedef struct {
    float *elev;  /* derived slot 0 (elevation) */
    float *temp;  /* sampled field 2 (temperature) */
} sink_t;

static const sink_t sink = { elev_out, temp_out };

/* Writer: stash raw derived elevation and sampled temperature per tile. */
static void
write_relief(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    const sink_t *s = (const sink_t *)data;
    const rlk_wg_t *wg = ctx->wg;
    out->biome = (uint8_t)ctx->zone;
    const size_t k = (size_t)ctx->y * MAP + (size_t)ctx->x;
    if (s->elev) s->elev[k] = ctx->field[wg->nsampled + 0];
    if (s->temp) s->temp[k] = ctx->field[2];
}

/* Minimal wg: 3 sampled fields (cont, hills/noise, temp), 1 full-coverage
   zone on field 0 writing into the sink. */
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
                                   .write = write_relief, .data = &sink };
    return wg;
}

/* Base relief cfg: continent only, elevation into derived slot 0. */
static rlk_wg_relief_cfg_t
cfg_min(void) {
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
    return r;
}

static void
run_fill(rlk_wg_t *wg) {
    rlk_wg_init(wg);
    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(wg, 0, 0, &tm);
}

static float
cont_at(const rlk_wg_t *wg, int x, int y) {
    return rlk_wg_sample_field(wg, 0, (float)x + 0.5f, (float)y + 0.5f);
}

static float
elev_at(int x, int y) {
    return elev_out[(size_t)y * MAP + (size_t)x];
}

/* Basic: with only f_cont, elevation equals the continent field exactly. */
static void
test_relief_stage_basic(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = cfg_min();
    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++)
            assert(elev_at(x, y) == cont_at(&wg, x, y));
}

/* Hills: elev = cont + hills_amp * (hills - 0.5), sampled at the tile centre. */
static void
test_relief_stage_hills(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = cfg_min();
    r.f_hills = 1;
    r.hills_amp = 0.30f;
    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float wxf = (float)x + 0.5f, wyf = (float)y + 0.5f;
            const float hills = rlk_wg_sample_field(&wg, 1, wxf, wyf);
            const float expected = cont_at(&wg, x, y) + 0.30f * (hills - 0.5f);
            assert(elev_at(x, y) == expected);
        }
}

/* Domain warp: hills are sampled at the warped position. */
static void
test_relief_stage_warp(void) {
    rlk_wg_t wg = wg_min();
    wg.nsampled = 5;
    wg.fields[3] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 4, .octaves = 2, .freq = 0.010f, .gain = 0.5f };
    wg.fields[4] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .salt = 5, .octaves = 2, .freq = 0.010f, .gain = 0.5f };
    rlk_wg_relief_cfg_t r = cfg_min();
    r.f_hills = 1;
    r.hills_amp = 0.30f;
    r.f_warp_x = 3;
    r.f_warp_y = 4;
    r.warp = 20.0f;
    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float wxf = (float)x + 0.5f, wyf = (float)y + 0.5f;
            const float vx = rlk_wg_sample_field(&wg, 3, wxf, wyf);
            const float vy = rlk_wg_sample_field(&wg, 4, wxf, wyf);
            const float px = wxf + (vx - 0.5f) * 2.0f * 20.0f;
            const float py = wyf + (vy - 0.5f) * 2.0f * 20.0f;
            const float hills = rlk_wg_sample_field(&wg, 1, px, py);
            const float expected = cont_at(&wg, x, y) + 0.30f * (hills - 0.5f);
            assert(elev_at(x, y) == expected);
        }
}

/* Mountains: elev = cont + mountain_amp * smoothstep(lo,hi,cont) * ridge. */
static void
test_relief_stage_mountains(void) {
    rlk_wg_t wg = wg_min();
    wg.fields[2] = (rlk_wg_field_t){ .kind = RLK_WG_RIDGE, .salt = 3, .octaves = 3, .freq = 0.020f, .gain = 0.5f };
    rlk_wg_relief_cfg_t r = cfg_min();
    r.f_ridge = 2;
    r.mountain_lo = 0.45f;
    r.mountain_hi = 0.65f;
    r.mountain_amp = 0.45f;
    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float wxf = (float)x + 0.5f, wyf = (float)y + 0.5f;
            const float cont = cont_at(&wg, x, y);
            const float mtn = rlk_smoothstep(rlk_clampf((cont - 0.45f) / (0.65f - 0.45f), 0.0f, 1.0f));
            const float ridge = rlk_wg_sample_field(&wg, 2, wxf, wyf);
            const float expected = cont + 0.45f * mtn * ridge;
            assert(elev_at(x, y) == expected);
        }
}

/* River carve: an earlier stage writes the carve slot; relief subtracts it
   on land only (elev > sea before carving), tapered near the coast so the
   carved elevation never goes below sea level. */
static void
carve_stage(rlk_wg_ctx_t *ctx, const void *data) {
    (void)data;
    ctx->field[ctx->wg->nsampled + 1] = 0.5f;
}

static void
test_relief_stage_river_carve(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = cfg_min();
    r.river_carve_field = 1;
    assert(rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = carve_stage, .data = NULL }) == RLK_OK);
    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float cont = cont_at(&wg, x, y);
            float expected = cont;
            if (cont > 0.40f) {
                float carve = 0.5f;
                const float room = cont - 0.40f;
                if (carve > room)
                    carve = room;
                expected = cont - carve;
                assert(expected >= 0.40f);  /* taper: never below sea */
            }
            assert(elev_at(x, y) == expected);
        }
}

/* Fjords: carve only subtracts, and at least one coastal tile is carved. */
static void
test_relief_stage_fjord(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = cfg_min();
    r.f_fjord = 1;
    r.fjord_dir[0] = 1.0f;
    r.fjord_dir[1] = 0.0f;
    r.fjord_reach = 0.30f;  /* wide coast band so some tiles carve */
    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    run_fill(&wg);

    int carved = 0;
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float cont = cont_at(&wg, x, y);
            const float e = elev_at(x, y);
            assert(e <= cont + 1e-4f);  /* fjord only subtracts */
            if (e < cont - 1e-4f)
                carved++;
        }
    assert(carved > 0);
}

/* Lapse: on land (elev > sea), temp -= lapse * (elev - sea), in place. */
static void
test_relief_stage_lapse(void) {
    rlk_wg_t wg = wg_min();
    rlk_wg_relief_cfg_t r = cfg_min();
    r.f_temp = 2;
    r.lapse = 0.35f;
    assert(rlk_wg_add(&wg, rlk_wg_relief(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float wxf = (float)x + 0.5f, wyf = (float)y + 0.5f;
            const float cont = cont_at(&wg, x, y);
            const float temp0 = rlk_wg_sample_field(&wg, 2, wxf, wyf);
            const float expected = cont > 0.40f ? temp0 - 0.35f * (cont - 0.40f) : temp0;
            assert(temp_out[(size_t)y * MAP + (size_t)x] == expected);
        }
}

/* Constructor: pins the zero defaults resolved in place (documented in the
   header) and the stage data pointer. */
static void
test_relief_constructor_defaults(void) {
    rlk_wg_relief_cfg_t r = cfg_min();
    rlk_wg_stage_t st = rlk_wg_relief(&r);
    assert(st.data == &r);
    assert(r.warp == 30.0f);
    assert(r.hills_amp == 0.30f);
    assert(r.mountain_lo == 0.45f && r.mountain_hi == 0.65f);
    assert(r.mountain_amp == 0.45f);
    assert(r.lapse == 0.35f);
    assert(r.fjord_stretch == 0.25f);
    assert(r.fjord_width == 0.05f);
    assert(r.fjord_depth == 0.45f);
    assert(r.fjord_steep == 3.0f);
    assert(r.fjord_reach == 0.12f);
    assert(r.fjord_cold0 == 0.35f && r.fjord_cold1 == 0.55f);
}

int
main(void) {
    test_relief_stage_basic();
    test_relief_stage_hills();
    test_relief_stage_warp();
    test_relief_stage_mountains();
    test_relief_stage_river_carve();
    test_relief_stage_fjord();
    test_relief_stage_lapse();
    test_relief_constructor_defaults();
    return 0;
}
