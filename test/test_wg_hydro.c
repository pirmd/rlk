#include <assert.h>
#include <stdint.h>
#include "wg_hydro.h"
#include "fmath.h"

#define MAP 16

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: derived[0]=slope -> type (1 if > 0), derived[1]=strahler -> variant,
   derived[2]=speed -> flags (255 if > 0). */
static void
write_hydro(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    const rlk_wg_t *wg = ctx->wg;
    out->biome = (uint8_t)ctx->zone;
    out->type  = ctx->field[wg->nsampled + 0] > 0.0f ? 1 : 0;
    out->variant = (uint8_t)ctx->field[wg->nsampled + 1];
    out->flags = ctx->field[wg->nsampled + 2] > 0.0f ? 255 : 0;
}

/* Minimal wg: 1 sampled field, 3 derived fields, 1 full-coverage zone. */
static rlk_wg_t
wg_min(void) {
    rlk_wg_t wg = {0};
    wg.seed = 42ULL;
    wg.inside_scale = 0.20f;
    wg.border_width = 0.20f;
    wg.nsampled = 1;
    wg.nderived = 3;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_axis_t){ .slot = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_hydro };
    return wg;
}

/* Hydro fixture: 4x4 cells of 4 tiles, slope eastward (elev = i, flow west). */
#define FW 4
#define FH 4
#define FCELL 4

static float elev_buf[FW * FH];
static uint16_t filled_buf[FW * FH];
static uint8_t dir_buf[FW * FH];
static uint32_t acc_buf[FW * FH];
static int order_buf[FW * FH];
static uint64_t drain_scratch[FW * FH];

static float slope_buf[FW * FH];
static uint8_t strahler_buf[FW * FH];
static float speed_buf[FW * FH];
static uint16_t hydro_scratch[FW * FH];

static rlk_grid_t elev;
static rlk_drain_t drain;
static rlk_hydro_t hydro;

static void
hydro_setup(void) {
    elev = (rlk_grid_t){ .lattice = { .w = FW, .h = FH, .cellsize = FCELL,
                                       .origin_x = 0, .origin_y = 0 }, .values = elev_buf };
    for (int j = 0; j < FH; j++)
        for (int i = 0; i < FW; i++)
            elev.values[(size_t)j * FW + (size_t)i] = (float)i;
    drain = (rlk_drain_t){ .lattice = elev.lattice, .filled = filled_buf,
                           .dir = dir_buf, .acc = acc_buf, .order = order_buf };
    assert(rlk_drain_build(&drain, &elev, -1.0f, drain_scratch) == RLK_OK);
    hydro = (rlk_hydro_t){ .lattice = drain.lattice, .slope = slope_buf,
                           .strahler = strahler_buf, .speed = speed_buf };
    rlk_hydro_build(&hydro, &drain, 1u, hydro_scratch);
}

/* Basic: stage projects slope/strahler/speed into derived fields. */
static void
test_hydro_stage_basic(void) {
    hydro_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_hydro_cfg_t f = {0};
    f.hydro = &hydro;
    f.slot_slope = 0;
    f.slot_strahler = 1;
    f.slot_speed = 2;

    assert(rlk_wg_add(&wg, rlk_wg_hydro(&f)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Tile (2,2) -> cell (0,0): west edge = outlet -> slope 0, speed 0 */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 2, 2);
    assert(t);
    assert(t->type == 0);
    assert(t->flags == 0);
    assert(t->variant == 1);  /* linear network: Strahler 1 everywhere */

    /* Interior tile (6,6) -> cell (1,1): flows westward -> slope > 0 */
    t = rlk_tilemap_get(&tm, 6, 6);
    assert(t);
    assert(t->type == 1);
    assert(t->flags == 255);
    assert(t->variant == 1);
}

/* NONE slots: disabled outputs are not written (no crash). */
static void
test_hydro_stage_none_slots(void) {
    hydro_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_hydro_cfg_t f = {0};
    f.hydro = &hydro;
    f.slot_slope = RLK_WG_NONE;
    f.slot_strahler = 1;
    f.slot_speed = RLK_WG_NONE;

    assert(rlk_wg_add(&wg, rlk_wg_hydro(&f)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    const rlk_tile_t *t = rlk_tilemap_get(&tm, 6, 6);
    assert(t);
    assert(t->variant == 1);
}

/* Tiles outside the hydro grid get 0. */
static void
test_hydro_stage_outside(void) {
    hydro_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_hydro_cfg_t f = {0};
    f.hydro = &hydro;
    f.slot_slope = 0;
    f.slot_strahler = 1;
    f.slot_speed = 2;

    assert(rlk_wg_add(&wg, rlk_wg_hydro(&f)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    tm.origin_x = 1000; tm.origin_y = 1000;
    rlk_wg_fill(&wg, 1000, 1000, &tm);  /* far outside the 16x16 grid */

    const rlk_tile_t *t = rlk_tilemap_get(&tm, 1003, 1003);
    assert(t);
    assert(t->type == 0);
    assert(t->variant == 0);
    assert(t->flags == 0);
}

/* Main */

int
main(void) {
    test_hydro_stage_basic();
    test_hydro_stage_none_slots();
    test_hydro_stage_outside();
    return 0;
}
