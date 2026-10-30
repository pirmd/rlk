#include <assert.h>
#include <stdint.h>
#include "wg_grid.h"

#define MAP 16

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: copies derived field[nsampled] into variant (normalized [0,1] -> 0..255). */
static void
write_derived(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->biome = (uint8_t)ctx->zone;
    out->variant = (uint8_t)(rlk_clampf(ctx->field[ctx->wg->nsampled], 0.0f, 1.0f) * 255.0f);
}

/* Minimal wg: 1 sampled field, 1 derived field, 1 full-coverage zone. */
static rlk_wg_t
wg_min(void) {
    rlk_wg_t wg = {0};
    wg.seed = 42ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 1;
    wg.nderived = 1;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_axis_t){ .field = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_derived };
    return wg;
}

/* Grid fixture: 4x4 cells of 8 tiles, origin (0,0), constant value 0.5. */
#define GW 4
#define GH 4
#define GCELL 8

static float grid_buf[GW * GH];
static rlk_grid_t grid;

static void
grid_setup(void) {
    grid = (rlk_grid_t){ .lattice = { .w = GW, .h = GH, .cellsize = GCELL, .origin_x = 0, .origin_y = 0 }, .v = grid_buf };
    for (int k = 0; k < GW * GH; k++)
        grid_buf[k] = 0.5f;
}

/* Basic: sample 0.5 with scale=0 (default 1.0), offset=0 -> variant = 127 */
static void
test_grid_stage_basic(void) {
    grid_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_grid_cfg_t g = {0};
    g.grid = &grid;
    g.slot = 0;

    assert(rlk_wg_add(&wg, rlk_wg_grid(&g)) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

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

    rlk_wg_t wg = wg_min();
    rlk_wg_grid_cfg_t g = {0};
    g.grid = &grid;
    g.slot = 0;
    g.scale = 2.0f;
    g.offset = 1.0f;

    assert(rlk_wg_add(&wg, rlk_wg_grid(&g)) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    const rlk_tile_t *t = rlk_tilemap_get(&tm, 12, 12);
    assert(t);
    /* 0.5 * 2.0 + 1.0 = 2.0 -> clamped to 255 */
    assert(t->variant == 255);
}

/* Constructor builds a stage from a const config (fn is static, only the
   data pointer is checkable). */
static void
test_grid_constructor(void) {
    grid_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_grid_cfg_t g = {0};
    g.grid = &grid;
    g.slot = 0;

    rlk_wg_stage_t st = rlk_wg_grid(&g);
    assert(st.data == &g);
    /* cfg is const: scale 0 -> 1.0 resolved in the stage, not written back. */
    assert(g.scale == 0.0f);

    assert(rlk_wg_add(&wg, st) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].data == &g);

    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);
}

/* Stage table full -> RLK_E_INVALID. */
static void
test_grid_limit_too_many_stages(void) {
    grid_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_grid_cfg_t g = {0};
    g.grid = &grid;
    g.slot = 0;
    for (int i = 0; i < RLK_WG_MAX_STAGES; i++)
        assert(rlk_wg_add(&wg, rlk_wg_grid(&g)) == RLK_OK);

    assert(rlk_wg_add(&wg, rlk_wg_grid(&g)) == RLK_E_INVALID);
}

int
main(void) {
    test_grid_stage_basic();
    test_grid_stage_scale_offset();
    test_grid_constructor();
    test_grid_limit_too_many_stages();
    return 0;
}
