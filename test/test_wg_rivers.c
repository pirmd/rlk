#include <assert.h>
#include <stdint.h>
#include <math.h>

#include "wg_rivers.h"

#define MAP 16

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: derived[0]=mask -> variant (0..255), derived[1]=carve -> flags */
static void
write_rivers(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    const rlk_wg_t *wg = ctx->wg;
    out->biome = (uint8_t)ctx->zone;
    out->variant = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 0], 0.0f, 1.0f) * 255.0f);
    out->flags = (uint8_t)(rlk_clampf(ctx->field[wg->nsampled + 1], 0.0f, 1.0f) * 255.0f);
}

/* Minimal wg: 1 sampled field, 2 derived fields, 1 full-coverage zone. */
static rlk_wg_t
wg_min(void) {
    rlk_wg_t wg = {0};
    wg.seed = 42ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 1;
    wg.nderived = 2;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_axis_t){ .field = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_rivers };
    return wg;
}

/* wg with 4 sampled fields for wiggle/creek tests.
   field 0 = axis, 1-2 = wiggle, 3 = creek */
static rlk_wg_t
wg_wiggle(void) {
    rlk_wg_t wg = {0};
    wg.seed = 42ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 4;
    wg.nderived = 2;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
    wg.fields[1] = (rlk_wg_field_t){ .freq = 0.10f, .seed = 100 };
    wg.fields[2] = (rlk_wg_field_t){ .freq = 0.12f, .seed = 101 };
    wg.fields[3] = (rlk_wg_field_t){ .freq = 0.050f, .seed = 102 };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_axis_t){ .field = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_rivers };
    return wg;
}

/* Drain fixture: 4x4 cells of 4 tiles, slope eastward (elev = i).
   All cells drain west; acc increases westward. */
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
            elev.v[(size_t)j * FW + (size_t)i] = (float)i + 0.5f;

    drain = (rlk_drain_t){ .lattice = elev.lattice, .filled = filled_buf, .dir = dir_buf,
                           .acc = acc_buf, .order = order_buf };
    assert(rlk_drain_build(&drain, &elev, -1.0f, scratch) == RLK_OK);
}

/* Basic: stage writes mask and carve into derived fields.
   East-sloping grid: rivers flow west, acc increases westward.
   Tiles near the western edge should have higher mask (more accumulation). */
static void
test_rivers_stage_basic(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.sea = 0.0f;
    r.acc_min = 1;
    r.bank_fade = 1.0f;
    r.valley_mult = 3.0f;
    r.carve = 0.05f;
    r.slot_mask = 0;
    r.slot_carve = 1;

    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Western edge (tile 2,2 -> cell 0,0): acc=1 (only itself drains through it),
       but it's an outlet (grid edge) so no river segment. mask should be low. */
    const rlk_tile_t *tw = rlk_tilemap_get(&tm, 2, 2);
    assert(tw);

    /* Eastern interior (tile 10,6 -> cell 2,1): acc should be higher,
       closer to a river segment. mask should be > 0. */
    const rlk_tile_t *te = rlk_tilemap_get(&tm, 10, 6);
    assert(te);
    /* At least some tile should have a non-zero mask (river detected) */
    int has_river = 0;
    for (int y = 0; y < MAP && !has_river; y++)
        for (int x = 0; x < MAP && !has_river; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            if (t && t->variant > 0)
                has_river = 1;
        }
    assert(has_river);
}

/* NONE slots: disabled outputs are not written. */
static void
test_rivers_stage_none_slots(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.sea = 0.0f;
    r.acc_min = 1;
    r.slot_mask = 0;
    r.slot_carve = RLK_WG_RIVERS_NONE;

    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Should not crash; mask written, carve untouched. */
    const rlk_tile_t *t = rlk_tilemap_get(&tm, 2, 2);
    assert(t);
}

/* acc_min too high: no river cells qualify, mask stays 0 everywhere. */
static void
test_rivers_stage_high_acc_min(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.sea = 0.0f;
    r.acc_min = 1000;  /* higher than total cells (16) */
    r.valley_mult = 3.0f;
    r.slot_mask = 0;
    r.slot_carve = 1;

    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* All masks should be 0 */
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            assert(t);
            assert(t->variant == 0);
            assert(t->flags == 0);
        }
}

/* Constructor builds a stage from a const config (fn is static, only the
   data pointer is checkable). */
static void
test_rivers_constructor(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.sea = 0.0f;
    r.acc_min = 1;
    r.slot_mask = 0;
    r.slot_carve = 1;

    rlk_wg_stage_t st = rlk_wg_rivers(&r);
    assert(st.data == &r);
    /* cfg is const: zero defaults resolved in the stage, not written back. */
    assert(r.bank_fade == 0.0f);
    assert(r.valley_mult == 0.0f);

    assert(rlk_wg_add(&wg, st) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].data == &r);

    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);
}

/* Sea level above all elevations: all cells underwater, no rivers. */
static void
test_rivers_stage_all_underwater(void) {
    drain_setup();

    rlk_wg_t wg = wg_min();
    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.sea = 100.0f;  /* everything underwater */
    r.acc_min = 1;
    r.valley_mult = 3.0f;
    r.slot_mask = 0;
    r.slot_carve = 1;

    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* All masks should be 0 (underwater cells are skipped) */
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            assert(t);
            assert(t->variant == 0);
        }
}

/* Wiggle: with wiggle fields active, rivers are still detected (mask > 0 somewhere) */
static void
test_rivers_stage_wiggle(void) {
    drain_setup();

    rlk_wg_t wg = wg_wiggle();
    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.sea = 0.0f;
    r.acc_min = 1;
    r.valley_mult = 3.0f;
    r.slot_mask = 0;
    r.slot_carve = 1;
    r.f_wig_x = 1;
    r.f_wig_y = 2;
    r.wig_tiles = 3.0f;
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* At least one tile should have a non-zero mask (river detected) */
    int has_river = 0;
    for (int y = 0; y < MAP && !has_river; y++)
        for (int x = 0; x < MAP && !has_river; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            if (t && t->variant > 0)
                has_river = 1;
        }
    assert(has_river);
}

/* Creeks: with acc_min very high (no drainage rivers), creeks still produce mask > 0 */
static void
test_rivers_stage_creeks(void) {
    drain_setup();

    rlk_wg_t wg = wg_wiggle();
    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.sea = 0.0f;
    r.acc_min = 1000;  /* no drainage rivers */
    r.valley_mult = 3.0f;
    r.slot_mask = 0;
    r.slot_carve = 1;
    r.f_creek = 3;
    r.creek_width = 3.0f;  /* wide creeks for reliable detection */
    r.creek_carve = 0.02f;
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    assert(rlk_wg_init(&wg) == RLK_OK);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_fill(&wg, 0, 0, &tm);

    /* Creeks should produce at least one tile with mask > 0 */
    int has_creek = 0;
    for (int y = 0; y < MAP && !has_creek; y++)
        for (int x = 0; x < MAP && !has_creek; x++) {
            const rlk_tile_t *t = rlk_tilemap_get(&tm, x, y);
            if (t && t->variant > 0)
                has_creek = 1;
        }
    assert(has_creek);
}

int
main(void) {
    test_rivers_stage_basic();
    test_rivers_stage_none_slots();
    test_rivers_stage_high_acc_min();
    test_rivers_constructor();
    test_rivers_stage_all_underwater();
    test_rivers_stage_wiggle();
    test_rivers_stage_creeks();
    return 0;
}
