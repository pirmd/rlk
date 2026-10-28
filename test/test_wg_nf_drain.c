#include <assert.h>
#include <stdint.h>
#include "wg_nf_drain.h"

#define MAP 16

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: derived[0]=acc -> variant, derived[1]=dir -> type, derived[2]=lake -> flags.
   acc and lake are normalized [0,1] -> 0..255; dir is raw 0..8. */
static void
write_drain(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    const rlk_wg_nf_t *wg = ctx->wg;
    out->biome = (uint8_t)ctx->zone;
    out->type  = (uint8_t)ctx->field[wg->nsampled + 1];
    out->flags = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 2], 0.0f, 1.0f) * 255.0f);
    out->variant = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 0], 0.0f, 1.0f) * 255.0f);
}

/* Minimal wg_nf: 1 sampled field, 3 derived fields, 1 full-coverage zone. */
static rlk_wg_nf_t
wg_min(void) {
    rlk_wg_nf_t wg = {0};
    wg.seed = 42ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 1;
    wg.nderived = 3;
    wg.fields[0] = (rlk_wg_nf_field_t){ .freq = 0.01f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_nf_axis_t){ .field = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_nf_zone_t){ .lo = {0.0f}, .hi = {1.0f},
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
    elev = (rlk_grid_t){ .lattice = { .w = FW, .h = FH, .cellsize = FCELL, .origin_x = 0, .origin_y = 0 }, .v = elev_buf };
    for (int j = 0; j < FH; j++)
        for (int i = 0; i < FW; i++)
            elev.v[(size_t)j * FW + (size_t)i] = (float)i;

    drain = (rlk_drain_t){ .lattice = elev.lattice, .filled = filled_buf, .dir = dir_buf,
                           .acc = acc_buf, .order = order_buf };
    assert(rlk_drain_build(&drain, &elev, -1.0f, scratch) == RLK_OK);
}

/* init: acc_scale 0 -> 1.0 */
static void
test_drain_init_defaults(void) {
    drain_setup();
    rlk_wg_nf_drain_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.sea = 0.0f;
    rlk_wg_nf_drain_init(&f);
    assert(f.acc_scale == 1.0f);
}

/* Basic: stage writes acc/dir/lake into derived fields. */
static void
test_drain_stage_basic(void) {
    drain_setup();

    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_drain_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.sea = -1.0f;
    f.slot_acc = 0;
    f.slot_dir = 1;
    f.slot_lake = 2;
    f.acc_scale = (float)(FW * FH);

    assert(rlk_wg_nf_drain_add(&wg, &f) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

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

    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_drain_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.sea = -1.0f;
    f.slot_acc = 0;
    f.slot_dir = RLK_WG_NF_DRAIN_NONE;
    f.slot_lake = RLK_WG_NF_DRAIN_NONE;

    assert(rlk_wg_nf_drain_add(&wg, &f) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* Should not crash; acc written, dir/lake untouched. */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 2, 2);
    assert(t);
}

/* Power-user: init then add_stage manually. */
static void
test_drain_init_then_add_stage(void) {
    drain_setup();

    rlk_wg_nf_t wg = wg_min();
    rlk_wg_nf_drain_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.sea = -1.0f;
    f.slot_acc = 0;
    f.slot_dir = 1;
    f.slot_lake = 2;

    rlk_wg_nf_drain_init(&f);
    assert(f.acc_scale == 1.0f);
    assert(rlk_wg_nf_add_stage(&wg, rlk_wg_nf_drain_stage, &f) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].fn == rlk_wg_nf_drain_stage);

    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);
}

/* Stage table full -> RLK_E_INVALID. */
static void
test_drain_limit_too_many_stages(void) {
    drain_setup();

    rlk_wg_nf_t wg = wg_min();
    for (int i = 0; i < RLK_WG_NF_MAX_STAGES; i++) {
        rlk_wg_nf_drain_t f = {0};
        f.drain = &drain;
        f.elev = &elev;
        f.slot_acc = 0;
        f.slot_dir = 1;
        f.slot_lake = 2;
        assert(rlk_wg_nf_drain_add(&wg, &f) == RLK_OK);
    }

    rlk_wg_nf_drain_t f = {0};
    f.drain = &drain;
    f.elev = &elev;
    f.slot_acc = 0;
    f.slot_dir = 1;
    f.slot_lake = 2;
    assert(rlk_wg_nf_drain_add(&wg, &f) == RLK_E_INVALID);
}

int
main(void) {
    test_drain_init_defaults();
    test_drain_stage_basic();
    test_drain_stage_none_slots();
    test_drain_init_then_add_stage();
    test_drain_limit_too_many_stages();
    return 0;
}
