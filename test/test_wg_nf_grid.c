#include <assert.h>
#include <stdint.h>
#include "wg_nf_grid.h"

#define MAP 16

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: copies derived field[nsampled] into variant (normalized [0,1] -> 0..255). */
static void
write_derived(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->biome = (uint8_t)ctx->zone;
    out->variant = (uint8_t)(rlk_clampf(ctx->field[ctx->wg->nsampled], 0.0f, 1.0f) * 255.0f);
}

/* Minimal wg_nf: 1 sampled field, 1 derived field, 1 full-coverage zone. */
static rlk_wg_nf_t
wg_min(void) {
    rlk_wg_nf_t wg = {0};
    wg.seed = 42ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 1;
    wg.nderived = 1;
    wg.fields[0] = (rlk_wg_nf_field_t){ .freq = 0.01f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_nf_axis_t){ .field = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_nf_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_derived };
    return wg;
}

/* Grid fixture: 4x4 cells of 8 tiles, origin (0,0), constant value 0.5. */
#define GW 4
#define GH 4
#define GCELL 8

static float grid_buf[GW * GH];
static rlk_wg_grid_t grid;

static float
const_fn(float px, float py, const void *user) {
    (void)px; (void)py; (void)user;
    return 0.5f;
}

static void
grid_setup(void) {
    rlk_wg_grid_init(&grid, GW, GH, GCELL, 0, 0, grid_buf);
    rlk_wg_grid_fill(&grid, const_fn, NULL);
}

/* init: scale 0 -> 1.0 */
static void
test_grid_init_defaults(void) {
    rlk_wg_nf_grid_t g = {0};
    g.grid = &grid;
    g.slot = 0;
    rlk_wg_nf_grid_init(&g);
    assert(g.scale == 1.0f);
    assert(g.offset == 0.0f);
}

/* Basic: sample 0.5 with scale=1, offset=0 -> field = 0.5 -> variant = 127 */
static void
test_grid_stage_basic(void) {
    grid_setup();

    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_grid_t g = {0};
    g.grid = &grid;
    g.slot = 0;

    assert(rlk_wg_nf_grid_add(&wg, &g) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* Centre of cell (1,1): world tile (12,12). Sample = 0.5.
       variant = 0.5 * 255 = 127.5 -> 127 */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 12, 12);
    assert(t);
    assert(t->variant == 127);
}

/* Scale and offset: 0.5 * 2.0 + 1.0 = 2.0 -> clamped to 255 */
static void
test_grid_stage_scale_offset(void) {
    grid_setup();

    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_grid_t g = {0};
    g.grid = &grid;
    g.slot = 0;
    g.scale = 2.0f;
    g.offset = 1.0f;

    assert(rlk_wg_nf_grid_add(&wg, &g) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    const rlk_tile_t *t = rlk_tilemap_get(&tm, 12, 12);
    assert(t);
    /* 0.5 * 2.0 + 1.0 = 2.0 -> clamped to 255 */
    assert(t->variant == 255);
}

/* Power-user: init then add_stage manually. */
static void
test_grid_init_then_add_stage(void) {
    grid_setup();

    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_grid_t g = {0};
    g.grid = &grid;
    g.slot = 0;

    rlk_wg_nf_grid_init(&g);
    assert(g.scale == 1.0f);
    assert(rlk_wg_nf_add_stage(&wg, rlk_wg_nf_grid_stage, &g) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].fn == rlk_wg_nf_grid_stage);

    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);
}

/* Stage table full -> RLK_E_INVALID. */
static void
test_grid_limit_too_many_stages(void) {
    grid_setup();

    rlk_wg_nf_t wg = wg_min();
    for (int i = 0; i < RLK_WG_NF_MAX_STAGES; i++) {
        rlk_wg_nf_grid_t g = {0};
        g.grid = &grid;
        g.slot = 0;
        assert(rlk_wg_nf_grid_add(&wg, &g) == RLK_OK);
    }

    rlk_wg_nf_grid_t g = {0};
    g.grid = &grid;
    g.slot = 0;
    assert(rlk_wg_nf_grid_add(&wg, &g) == RLK_E_INVALID);
}

int
main(void) {
    test_grid_init_defaults();
    test_grid_stage_basic();
    test_grid_stage_scale_offset();
    test_grid_init_then_add_stage();
    test_grid_limit_too_many_stages();
    return 0;
}
