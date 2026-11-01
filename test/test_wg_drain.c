#include <assert.h>
#include <stdint.h>
#include "wg_drain.h"

#define MAP 16

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: derived[0]=acc -> variant, derived[1]=dir -> type, derived[2]=fill depth -> flags.
   acc and fill depth are normalized [0,1] -> 0..255; dir is raw 0..8. */
static void
write_drain(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    const rlk_wg_t *wg = ctx->wg;
    out->biome = (uint8_t)ctx->zone;
    out->type  = (uint8_t)ctx->field[wg->nsampled + 1];
    out->flags = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 2], 0.0f, 1.0f) * 255.0f);
    out->variant = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 0], 0.0f, 1.0f) * 255.0f);
}

/* Minimal wg: 1 sampled field, 3 derived fields, 1 full-coverage zone. */
static rlk_wg_t
wg_min(void) {
    rlk_wg_t wg = {0};
    wg.seed = 42ULL;
    wg.inside_scale = 0.20f;
    wg.border_width = 0.20f;
    wg.nsampled = 1;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_axis_t){ .slot = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_drain };
    return wg;
}

/* Drain fixture: 4x4 cells of 4 tiles, slope eastward (elev = i). */
#define FW 4
#define FH 4
#define FCELL 4

static float elev_buf[FW * FH];
static rlk_grid_t elev;
static uint16_t filled_buf[FW * FH];
static uint8_t dir_buf[FW * FH];
static uint32_t acc_buf[FW * FH];
static int order_buf[FW * FH];
static uint64_t scratch[FW * FH];

static rlk_drain_t drain;

static void
drain_setup(void) {
    elev = (rlk_grid_t){ .lattice = { .w = FW, .h = FH, .cellsize = FCELL, .origin_x = 0, .origin_y = 0 }, .values = elev_buf };
    for (int j = 0; j < FH; j++)
        for (int i = 0; i < FW; i++)
            elev.values[(size_t)j * FW + (size_t)i] = (float)i;

    drain = (rlk_drain_t){ .lattice = elev.lattice, .filled = filled_buf, .dir = dir_buf,
                           .acc = acc_buf, .order = order_buf };
    assert(rlk_drain_build(&drain, &elev, -1.0f, scratch) == RLK_OK);
}

/* Basic: stage writes acc/dir/fill into derived fields. */
static void
test_drain_stage_basic(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_drain_cfg_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.slot_acc = 0;
    f.slot_dir = 1;
    f.slot_fill = 2;
    f.acc_scale = (float)(FW * FH);

    assert(rlk_wg_add(&wg, rlk_wg_drain(&f)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Tile (2,2) -> cell (0,0): west edge = outlet -> dir = 8 */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 2, 2);
    assert(t);
    assert(t->type == RLK_DRAIN_OUTLET);
    assert(t->variant >= 1);  /* acc >= 1 */

    /* Tile (14,2) -> cell (3,0): east edge = outlet too (grid edge) */
    t = rlk_tilemap_get(&tm, 14, 2);
    assert(t);
    assert(t->type == RLK_DRAIN_OUTLET);

    /* Interior tile (6,6) -> cell (1,1): flows westward (dir 3,4,5) */
    t = rlk_tilemap_get(&tm, 6, 6);
    assert(t);
    assert(t->type >= 3 && t->type <= 5);
}

/* NONE slots: disabled outputs are not written (no crash). */
static void
test_drain_stage_none_slots(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_drain_cfg_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.slot_acc = 0;
    f.slot_dir = RLK_WG_NONE;
    f.slot_fill = RLK_WG_NONE;

    assert(rlk_wg_add(&wg, rlk_wg_drain(&f)) == RLK_OK);
    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Should not crash; acc written, dir/fill untouched. */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 2, 2);
    assert(t);
}

/* Constructor builds a stage from a config (fn is static, only the
   data pointer is checkable). */
static void
test_drain_constructor(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_drain_cfg_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.slot_acc = 0;
    f.slot_dir = 1;
    f.slot_fill = 2;

    rlk_wg_stage_t st = rlk_wg_drain(&f);
    assert(st.data == &f);
    /* Zero defaults are resolved in place by the constructor. */
    assert(f.acc_scale == 1.0f);

    assert(rlk_wg_add(&wg, st) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].data == &f);

    rlk_wg_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);
}

/* Stage table full -> RLK_E_INVALID. */
static void
test_drain_limit_too_many_stages(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_drain_cfg_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.slot_acc = 0;
    f.slot_dir = 1;
    f.slot_fill = 2;
    for (int i = 0; i < RLK_WG_MAX_STAGES; i++)
        assert(rlk_wg_add(&wg, rlk_wg_drain(&f)) == RLK_OK);

    assert(rlk_wg_add(&wg, rlk_wg_drain(&f)) == RLK_E_INVALID);
}

int
main(void) {
    test_drain_stage_basic();
    test_drain_stage_none_slots();
    test_drain_constructor();
    test_drain_limit_too_many_stages();
    return 0;
}
