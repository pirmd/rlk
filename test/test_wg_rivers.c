#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "wg_rivers.h"
#include "fmath.h"

#define MAP 16

static rlk_tile_t tilebuf[MAP * MAP];

/* Raw output buffers filled by the zone writer (run-local tile index):
   derived slot 0 = mask, slot 1 = carve, slot 2 = lake. */
static float mask_out[MAP * MAP];
static float carve_out[MAP * MAP];
static float lake_out[MAP * MAP];

typedef struct {
    float *mask;
    float *carve;
    float *lake;
} sink_t;

static const sink_t sink = { mask_out, carve_out, lake_out };

static void
write_rivers(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    const sink_t *s = (const sink_t *)data;
    const rlk_wg_t *wg = ctx->wg;
    out->biome = (uint8_t)ctx->zone;
    const size_t k = (size_t)ctx->y * MAP + (size_t)ctx->x;
    if (s->mask) s->mask[k] = ctx->field[wg->nsampled + 0];
    if (s->carve) s->carve[k] = ctx->field[wg->nsampled + 1];
    if (s->lake) s->lake[k] = ctx->field[wg->nsampled + 2];
}

/* Minimal wg: nsampled sampled fields, 1 full-coverage zone on field 0. */
static rlk_wg_t
wg_fields(int n) {
    rlk_wg_t wg = {0};
    wg.seed = 42ULL;
    wg.inside_scale = 0.20f;
    wg.border_width = 0.20f;
    wg.nsampled = n;
    wg.fields[0] = (rlk_wg_field_t){ .freq = 0.01f };
    for (int i = 1; i < n; i++)
        wg.fields[i] = (rlk_wg_field_t){ .freq = 0.10f + 0.02f * (float)i, .salt = 100u + (uint64_t)i };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_axis_t){ .slot = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                   .write = write_rivers, .data = &sink };
    return wg;
}

/* Drain fixture: 4x4 cells of 4 tiles covering the 16x16 map, slope eastward
   (elev = i + 0.5): all cells drain westward, acc grows westward. */
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
static uint32_t acc_max;  /* max accumulation of the fixture */

static void
drain_setup(void) {
    elev = (rlk_grid_t){ .lattice = { .w = FW, .h = FH, .cellsize = FCELL,
                                       .origin_x = 0, .origin_y = 0 }, .values = elev_buf };
    for (int j = 0; j < FH; j++)
        for (int i = 0; i < FW; i++)
            elev.values[(size_t)j * FW + (size_t)i] = (float)i + 0.5f;

    drain = (rlk_drain_t){ .lattice = elev.lattice, .filled = filled_buf, .dir = dir_buf,
                           .acc = acc_buf, .order = order_buf };
    assert(rlk_drain_build(&drain, &elev, -1.0f, scratch) == RLK_OK);
    /* Max acc among non-outlet cells: outlets have no downstream segment, so
       the SDF draws no flowline for them. */
    acc_max = 0;
    for (int k = 0; k < FW * FH; k++)
        if (dir_buf[k] != RLK_DRAIN_OUTLET && acc_buf[k] > acc_max)
            acc_max = acc_buf[k];
    assert(acc_max >= 1);
}

/* Base rivers cfg on the fixture. */
static rlk_wg_rivers_cfg_t
cfg_min(void) {
    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.sea = 0.0f;
    r.acc_min = acc_max;  /* only the most-accumulated non-outlet cells are flowlines */
    r.bank_fade = 1.0f;
    r.valley_mult = 3.0f;
    r.carve = 0.05f;
    r.slot_mask = 0;
    r.slot_carve = 1;
    r.slot_lake = RLK_WG_NONE;
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
mask_at(int x, int y)  { return mask_out[(size_t)y * MAP + (size_t)x]; }
static float
carve_at(int x, int y) { return carve_out[(size_t)y * MAP + (size_t)x]; }
static float
lake_at(int x, int y)  { return lake_out[(size_t)y * MAP + (size_t)x]; }

/* Expected mask/carve of the SDF projection at tile (x, y) (no meander). */
static void
expect_sdf(float *emask, float *ecarve, int x, int y, uint32_t acc_min,
           float bank_fade, float valley_mult, float carve) {
    const float sdf = rlk_drain_flowline_sdf(&drain, x, y, acc_min);
    *emask = 0.0f;
    *ecarve = 0.0f;
    if (sdf < RLK_DRAIN_FAR) {
        *emask = rlk_clampf(1.0f - sdf / bank_fade, 0.0f, 1.0f);
        const float reach = valley_mult * (float)elev.lattice.cellsize;
        const float vall = rlk_clampf(1.0f - sdf / reach, 0.0f, 1.0f);
        *ecarve = carve * vall * vall;
    }
    *emask = rlk_clampf(*emask, 0.0f, 1.0f);  /* clamp at slot write */
}

/* Basic: mask/carve match the SDF projection formula exactly, per tile. */
static void
test_rivers_stage_basic(void) {
    drain_setup();

    rlk_wg_t wg = wg_fields(1);
    rlk_wg_rivers_cfg_t r = cfg_min();
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    run_fill(&wg);

    int any_river = 0;
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            float emask, ecarve;
            expect_sdf(&emask, &ecarve, x, y, r.acc_min, r.bank_fade, r.valley_mult, r.carve);
            assert(mask_at(x, y) == emask);
            assert(carve_at(x, y) == ecarve);
            if (emask > 0.0f) any_river = 1;
        }
    assert(any_river);
}

/* NONE slot: the carve slot is not written (stays 0), the mask slot is. */
static void
test_rivers_stage_none_slots(void) {
    drain_setup();

    rlk_wg_t wg = wg_fields(1);
    rlk_wg_rivers_cfg_t r = cfg_min();
    r.slot_carve = RLK_WG_NONE;
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    run_fill(&wg);

    int any_river = 0;
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            assert(carve_at(x, y) == 0.0f);
            if (mask_at(x, y) > 0.0f) any_river = 1;
        }
    assert(any_river);
}

/* acc_min above the total cell count: no flowline segment, all zeros. */
static void
test_rivers_stage_high_acc_min(void) {
    drain_setup();

    rlk_wg_t wg = wg_fields(1);
    rlk_wg_rivers_cfg_t r = cfg_min();
    r.acc_min = 1000;
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            assert(mask_at(x, y) == 0.0f);
            assert(carve_at(x, y) == 0.0f);
        }
}

/* Sea above all elevations: sea_clip zeroes mask and carve everywhere. */
static void
test_rivers_stage_all_underwater(void) {
    drain_setup();

    rlk_wg_t wg = wg_fields(1);
    rlk_wg_rivers_cfg_t r = cfg_min();
    r.sea = 100.0f;
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            assert(mask_at(x, y) == 0.0f);
            assert(carve_at(x, y) == 0.0f);
        }
}

/* Meanders: the SDF is sampled at the displaced position, continuously
   (float variant), not quantized to whole tiles. */
static void
test_rivers_stage_meander(void) {
    drain_setup();

    rlk_wg_t wg = wg_fields(3);  /* fields 1,2 = wiggle */
    rlk_wg_rivers_cfg_t r = cfg_min();
    r.f_wig_x = 1;
    r.f_wig_y = 2;
    r.wig_tiles = 3.0f;
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    run_fill(&wg);

    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float wxf0 = (float)x + 0.5f, wyf0 = (float)y + 0.5f;
            const float vx = rlk_wg_sample_field(&wg, 1, wxf0, wyf0);
            const float vy = rlk_wg_sample_field(&wg, 2, wxf0, wyf0);
            const float wxf = wxf0 + (vx - 0.5f) * 2.0f * 3.0f;
            const float wyf = wyf0 + (vy - 0.5f) * 2.0f * 3.0f;
            const float sdf = rlk_drain_flowline_sdf_f(&drain, wxf, wyf, r.acc_min);
            float emask = 0.0f;
            if (sdf < RLK_DRAIN_FAR)
                emask = rlk_clampf(1.0f - sdf / r.bank_fade, 0.0f, 1.0f);
            emask = rlk_clampf(emask, 0.0f, 1.0f);
            assert(mask_at(x, y) == emask);
        }
}

/* Creeks: with acc_min high (no drainage rivers), the mask comes from the
   noise level-line formula alone. */
static void
test_rivers_stage_creeks(void) {
    drain_setup();

    rlk_wg_t wg = wg_fields(2);  /* field 1 = creek noise */
    rlk_wg_rivers_cfg_t r = cfg_min();
    r.acc_min = 1000;
    r.f_creek = 1;
    r.creek_width = 3.0f;
    r.creek_carve = 0.02f;
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    run_fill(&wg);

    int any_creek = 0;
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float wxf = (float)x + 0.5f, wyf = (float)y + 0.5f;
            const float cv = rlk_wg_sample_field(&wg, 1, wxf, wyf);
            const float cx = rlk_wg_sample_field(&wg, 1, wxf + 1.0f, wyf) - cv;
            const float cy = rlk_wg_sample_field(&wg, 1, wxf, wyf + 1.0f) - cv;
            float g = sqrtf(cx * cx + cy * cy);
            if (g < 0.004f) g = 0.004f;
            const float dist = fabsf(cv - 0.5f) / g;
            float emask = 1.0f - dist / 3.0f;
            if (emask < 0.0f) emask = 0.0f;  /* max with the initial 0 */
            emask = rlk_clampf(emask, 0.0f, 1.0f);
            const float vall = 1.0f - dist / (r.valley_mult * r.creek_width);
            float ecarve = 0.0f;
            if (vall > 0.0f)
                ecarve = r.creek_carve * vall * vall;
            assert(mask_at(x, y) == emask);
            assert(carve_at(x, y) == ecarve);
            if (emask > 0.0f) any_creek = 1;
        }
    assert(any_creek);
}

/* Lake overlay: written to slot_lake, kept OUT of the river mask; with no
   rivers the mask is 0 everywhere and slot_lake is exactly the smoothed
   lake-depth threshold. */
static void
test_rivers_stage_lake(void) {
    drain_setup();

    static float lake_buf[FW * FH];
    for (int k = 0; k < FW * FH; k++)
        lake_buf[k] = 0.0f;
    lake_buf[3 * FW + 3] = 1.0f;  /* one deep cell at the SE corner */
    rlk_grid_t lake = { .lattice = elev.lattice, .values = lake_buf };

    rlk_wg_t wg = wg_fields(1);
    rlk_wg_rivers_cfg_t r = cfg_min();
    r.acc_min = 1000;  /* no drainage rivers */
    r.lake_depth = &lake;
    r.lake_min_depth = 0.5f;
    r.slot_lake = 2;
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    run_fill(&wg);

    int any_lake = 0;
    for (int y = 0; y < MAP; y++)
        for (int x = 0; x < MAP; x++) {
            const float depth = rlk_grid_get(&lake, (float)x + 0.5f, (float)y + 0.5f);
            float elake = 0.0f;
            if (depth >= 0.5f) {
                const float lm = (depth - 0.5f * 0.5f) / (0.5f * 0.5f);
                elake = rlk_clampf(lm, 0.0f, 1.0f);
            }
            assert(mask_at(x, y) == 0.0f);   /* pure river mask: no lake merge */
            assert(lake_at(x, y) == elake);
            if (elake > 0.0f) any_lake = 1;
        }
    assert(any_lake);
}

/* Log-width law (specialist review 2026-10-30): acc_log_gain > 0 widens the
   bank fade with log10(acc), so the total mask strictly grows; gain <= 0
   switches the rule off. */
static unsigned long
mask_sum(rlk_wg_t *wg) {
    run_fill(wg);
    unsigned long sum = 0;
    for (int k = 0; k < MAP * MAP; k++)
        sum += (unsigned long)(mask_out[k] * 255.0f);
    return sum;
}

static void
test_rivers_stage_acc_log(void) {
    drain_setup();

    rlk_wg_rivers_cfg_t r = cfg_min();
    r.acc_log_gain = 2.0f;  /* strong gain for a clear widening */
    rlk_wg_t wg = wg_fields(1);
    assert(rlk_wg_add(&wg, rlk_wg_rivers(&r)) == RLK_OK);
    const unsigned long sum_hack = mask_sum(&wg);
    assert(sum_hack > 0);

    rlk_wg_rivers_cfg_t r2 = cfg_min();  /* gain 0: rule off */
    rlk_wg_t wg2 = wg_fields(1);
    assert(rlk_wg_add(&wg2, rlk_wg_rivers(&r2)) == RLK_OK);
    const unsigned long sum_off = mask_sum(&wg2);

    assert(sum_hack > sum_off);
}

/* Constructor: pins the zero defaults resolved in place (documented in the
   header) and the stage data pointer. */
static void
test_rivers_constructor_defaults(void) {
    drain_setup();

    rlk_wg_rivers_cfg_t r = {0};
    r.drain = &drain;
    r.elev = &elev;
    r.f_wig_x = 1;
    r.f_wig_y = 2;
    r.wig_tiles = 0.0f;      /* <= 0 disables meanders */
    r.f_creek = 1;
    r.creek_width = 0.0f;    /* <= 0 disables creeks */
    rlk_wg_stage_t st = rlk_wg_rivers(&r);
    assert(st.data == &r);
    assert(r.acc_min == 1);
    assert(r.bank_fade == 1.0f);
    assert(r.valley_mult == 1.0f);
    assert(r.f_wig_x == RLK_WG_NONE && r.f_wig_y == RLK_WG_NONE);
    assert(r.f_creek == RLK_WG_NONE);
}

int
main(void) {
    test_rivers_stage_basic();
    test_rivers_stage_none_slots();
    test_rivers_stage_high_acc_min();
    test_rivers_stage_all_underwater();
    test_rivers_stage_meander();
    test_rivers_stage_creeks();
    test_rivers_stage_lake();
    test_rivers_stage_acc_log();
    test_rivers_constructor_defaults();
    return 0;
}
