#include <assert.h>
#include <stdint.h>

#include "wg_nf_poi.h"

#define MAP 16

/* Game-defined POI types (the library defines none). */
enum {
    POI_CAVE = 1,
    POI_DUNGEON = 2,
};

#define POI_CAP 256

static rlk_tile_t tilebuf[MAP * MAP];

/* Writer: minimal, just sets biome. */
static void
write_min(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)ctx; (void)data;
    out->biome = 0;
}

/* Minimal wg_nf: 1 sampled field, 0 derived, 1 full-coverage zone. */
static rlk_wg_nf_t
wg_min(void) {
    rlk_wg_nf_t wg = {0};
    wg.seed = 42ULL;
    wg.depth_scale = 0.20f;
    wg.transition_width = 0.20f;
    wg.nsampled = 1;
    wg.nderived = 0;
    wg.fields[0] = (rlk_wg_nf_field_t){ .freq = 0.01f };
    wg.naxes = 1;
    wg.axes[0] = (rlk_wg_nf_axis_t){ .field = 0 };
    wg.nzones = 1;
    wg.zones[0] = (rlk_wg_nf_zone_t){ .lo = {0.0f}, .hi = {1.0f},
                                        .write = write_min };
    return wg;
}

/* init: density 0->0.001, size 0->32, min_separation 0->size. */
static void
test_poi_init_defaults(void) {
    rlk_poi_t poi_buf[POI_CAP];
    rlk_poi_list_t list = { poi_buf, 0, POI_CAP };
    rlk_wg_nf_poi_t cfg = {0};
    cfg.list = &list;
    cfg.type = POI_CAVE;
    cfg.field = 0;
    cfg.threshold = 0.5f;
    rlk_wg_nf_poi_init(&cfg);
    assert(cfg.density == 0.001f);
    assert(cfg.size == 32);
    assert(cfg.min_separation == 32);
}

/* Basic: density=1.0, threshold=0.0 -> every tile creates a POI
 * (until dedup kicks in with min_separation). */
static void
test_poi_stage_basic(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_poi_t poi_buf[POI_CAP];
    rlk_poi_list_t list = { poi_buf, 0, POI_CAP };

    rlk_wg_nf_poi_t cfg = {0};
    cfg.list = &list;
    cfg.type = POI_CAVE;
    cfg.field = 0;
    cfg.threshold = 0.0f;   /* field[0] in [0,1] always > 0 */
    cfg.density = 1.0f;     /* always trigger */
    cfg.size = 16;
    cfg.min_separation = 0; /* disable dedup for this test */
    cfg.salt = 0xABC;

    assert(rlk_wg_nf_poi_add(&wg, &cfg) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* With density=1.0 and no dedup, every tile should create a POI,
     * but the list is capped at POI_CAP. */
    assert(list.n > 0);
    assert(list.n <= POI_CAP);
    assert(list.v[0].type == (uint32_t)POI_CAVE);
    assert(list.v[0].bbox.w == 16);
    assert(list.v[0].bbox.h == 16);
    /* bbox is centred on the anchor and contains it; gen/level are zeroed */
    assert(list.v[0].bbox.x <= list.v[0].x && list.v[0].x < list.v[0].bbox.x + list.v[0].bbox.w);
    assert(list.v[0].bbox.y <= list.v[0].y && list.v[0].y < list.v[0].bbox.y + list.v[0].bbox.h);
    assert(list.v[0].gen == 0);
    assert(list.v[0].level == 0);
}

/* Below threshold: field <= threshold -> no POI. */
static void
test_poi_stage_below_threshold(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_poi_t poi_buf[POI_CAP];
    rlk_poi_list_t list = { poi_buf, 0, POI_CAP };

    rlk_wg_nf_poi_t cfg = {0};
    cfg.list = &list;
    cfg.type = POI_CAVE;
    cfg.field = 0;
    cfg.threshold = 1.1f;   /* field[0] in [0,1] never exceeds 1.1 */
    cfg.density = 1.0f;
    cfg.min_separation = 0;

    assert(rlk_wg_nf_poi_add(&wg, &cfg) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    assert(list.n == 0);
}

/* Dedup: min_separation prevents POIs closer than N tiles. */
static void
test_poi_stage_dedup(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_poi_t poi_buf[POI_CAP];
    rlk_poi_list_t list = { poi_buf, 0, POI_CAP };

    rlk_wg_nf_poi_t cfg = {0};
    cfg.list = &list;
    cfg.type = POI_CAVE;
    cfg.field = 0;
    cfg.threshold = 0.0f;
    cfg.density = 1.0f;
    cfg.size = 8;
    cfg.min_separation = 8;  /* at least 8 tiles apart */
    cfg.salt = 0xDEF;

    assert(rlk_wg_nf_poi_add(&wg, &cfg) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* With 16x16 map and separation 8, expect a handful of POIs. */
    assert(list.n > 0);
    assert(list.n < MAP * MAP);

    /* Verify separation: no two POIs closer than 8 tiles. */
    for (int i = 0; i < list.n; i++) {
        for (int j = i + 1; j < list.n; j++) {
            int dx = list.v[i].x - list.v[j].x;
            int dy = list.v[i].y - list.v[j].y;
            int d2 = dx * dx + dy * dy;
            assert(d2 >= 64);  /* 8^2 */
        }
    }
}

/* Deterministic: same seed + same config -> same POIs. */
static void
test_poi_stage_deterministic(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_poi_t buf1[POI_CAP], buf2[POI_CAP];
    rlk_poi_list_t list1 = { buf1, 0, POI_CAP };
    rlk_poi_list_t list2 = { buf2, 0, POI_CAP };

    rlk_wg_nf_poi_t cfg1 = {0};
    cfg1.list = &list1;
    cfg1.type = POI_CAVE;
    cfg1.field = 0;
    cfg1.threshold = 0.5f;
    cfg1.density = 0.1f;
    cfg1.size = 16;
    cfg1.min_separation = 4;
    cfg1.salt = 0x123;

    rlk_wg_nf_poi_t cfg2 = cfg1;
    cfg2.list = &list2;

    rlk_wg_nf_poi_add(&wg, &cfg1);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* Reset wg and list, re-run with same config. */
    wg = wg_min();
    rlk_wg_nf_poi_add(&wg, &cfg2);
    rlk_wg_nf_init(&wg);
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    assert(list1.n == list2.n);
    for (int i = 0; i < list1.n; i++) {
        assert(list1.v[i].x == list2.v[i].x);
        assert(list1.v[i].y == list2.v[i].y);
        assert(list1.v[i].seed == list2.v[i].seed);
    }
}

/* Drain constraint: only tiles with acc > min_acc create POIs. */
#define FW 4
#define FH 4
#define FCELL 4

static float elev_buf[FW * FH];
static rlk_grid_t elev_grid;
static uint16_t filled_buf[FW * FH];
static uint8_t dir_buf[FW * FH];
static uint32_t acc_buf[FW * FH];
static int order_buf[FW * FH];
static uint64_t scratch[FW * FH];
static rlk_drain_t drain;

static void
drain_setup(void) {
    elev_grid = (rlk_grid_t){ .lattice = { .w = FW, .h = FH, .cellsize = FCELL, .origin_x = 0, .origin_y = 0 }, .v = elev_buf };
    for (int j = 0; j < FH; j++)
        for (int i = 0; i < FW; i++)
            elev_grid.v[(size_t)j * FW + (size_t)i] = (float)i;

    drain = (rlk_drain_t){ .lattice = elev_grid.lattice, .filled = filled_buf, .dir = dir_buf,
                           .acc = acc_buf, .order = order_buf };
    assert(rlk_drain_build(&drain, &elev_grid, -1.0f, scratch) == RLK_OK);
}

static void
test_poi_stage_drain_constraint(void) {
    drain_setup();

    rlk_wg_nf_t wg = wg_min();
    rlk_poi_t poi_buf[POI_CAP];
    rlk_poi_list_t list = { poi_buf, 0, POI_CAP };

    rlk_wg_nf_poi_t cfg = {0};
    cfg.list = &list;
    cfg.type = POI_CAVE;
    cfg.field = 0;
    cfg.threshold = 0.0f;
    cfg.density = 1.0f;
    cfg.size = 8;
    cfg.min_separation = 0;
    cfg.drain = &drain;
    cfg.min_acc = 1.0f;  /* all cells have acc >= 1 */
    cfg.salt = 0xF10;

    assert(rlk_wg_nf_poi_add(&wg, &cfg) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* Every cell qualifies: POIs are created */
    assert(list.n > 0);

    /* min_acc above every accumulation: the constraint rejects all tiles */
    rlk_wg_nf_t wg2 = wg_min();
    rlk_poi_t poi_buf2[POI_CAP];
    rlk_poi_list_t list2 = { poi_buf2, 0, POI_CAP };
    rlk_wg_nf_poi_t cfg2 = cfg;
    cfg2.list = &list2;
    cfg2.min_acc = 1000.0f;  /* higher than total cells (16) */

    assert(rlk_wg_nf_poi_add(&wg2, &cfg2) == RLK_OK);
    rlk_wg_nf_init(&wg2);
    rlk_wg_nf_fill(&wg2, 0, 0, &tm);

    assert(list2.n == 0);
}

/* Grid constraint: only tiles with grid value in [min, max] create POIs. */
#define GW 4
#define GH 4
#define GCELL 4

static float grid_buf[GW * GH];
static rlk_grid_t val_grid;

static void
grid_setup(void) {
    val_grid = (rlk_grid_t){ .lattice = { .w = GW, .h = GH, .cellsize = GCELL, .origin_x = 0, .origin_y = 0 }, .v = grid_buf };
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++) {
            float px, py;
            rlk_grid_center(&val_grid, i, j, &px, &py);
            grid_buf[(size_t)j * GW + (size_t)i] = px / ((float)GW * (float)GCELL);
        }
}

static void
test_poi_stage_grid_constraint(void) {
    grid_setup();

    rlk_wg_nf_t wg = wg_min();
    rlk_poi_t poi_buf[POI_CAP];
    rlk_poi_list_t list = { poi_buf, 0, POI_CAP };

    rlk_wg_nf_poi_t cfg = {0};
    cfg.list = &list;
    cfg.type = POI_DUNGEON;
    cfg.field = 0;
    cfg.threshold = 0.0f;
    cfg.density = 1.0f;
    cfg.size = 8;
    cfg.min_separation = 0;
    cfg.grid = &val_grid;
    cfg.grid_min = 0.5f;
    cfg.grid_max = 0.75f;
    cfg.salt = 0x9D;

    assert(rlk_wg_nf_poi_add(&wg, &cfg) == RLK_OK);
    rlk_wg_nf_init(&wg);

    rlk_tilemap_t tm;
    rlk_tilemap_init(&tm, MAP, MAP, tilebuf, sizeof(tilebuf));
    rlk_wg_nf_fill(&wg, 0, 0, &tm);

    /* Only tiles with grid value in [0.5, 0.75] should create POIs.
     * Ramp goes from 0 to ~1 across 16 tiles; ~4 tiles in range. */
    assert(list.n > 0);
    assert(list.n < MAP * MAP);
}

/* Power-user: init then add_stage manually. */
static void
test_poi_init_then_add_stage(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_poi_t poi_buf[POI_CAP];
    rlk_poi_list_t list = { poi_buf, 0, POI_CAP };

    rlk_wg_nf_poi_t cfg = {0};
    cfg.list = &list;
    cfg.type = POI_CAVE;
    cfg.field = 0;
    cfg.threshold = 0.5f;

    rlk_wg_nf_poi_init(&cfg);
    assert(cfg.density == 0.001f);
    assert(cfg.size == 32);
    assert(cfg.min_separation == 32);
    assert(rlk_wg_nf_add_stage(&wg, rlk_wg_nf_poi_stage, &cfg) == RLK_OK);
    assert(wg.nstages == 1);
    assert(wg.stages[0].fn == rlk_wg_nf_poi_stage);
}

/* Stage table full -> RLK_E_INVALID. */
static void
test_poi_limit_too_many_stages(void) {
    rlk_wg_nf_t wg = wg_min();
    rlk_poi_t poi_buf[POI_CAP];
    rlk_poi_list_t list = { poi_buf, 0, POI_CAP };

    for (int i = 0; i < RLK_WG_NF_MAX_STAGES; i++) {
        rlk_wg_nf_poi_t cfg = {0};
        cfg.list = &list;
        cfg.type = POI_CAVE;
        cfg.field = 0;
        cfg.threshold = 0.5f;
        assert(rlk_wg_nf_poi_add(&wg, &cfg) == RLK_OK);
    }

    rlk_wg_nf_poi_t cfg = {0};
    cfg.list = &list;
    cfg.type = POI_CAVE;
    cfg.field = 0;
    cfg.threshold = 0.5f;
    assert(rlk_wg_nf_poi_add(&wg, &cfg) == RLK_E_INVALID);
}

int
main(void) {
    test_poi_init_defaults();
    test_poi_stage_basic();
    test_poi_stage_below_threshold();
    test_poi_stage_dedup();
    test_poi_stage_deterministic();
    test_poi_stage_drain_constraint();
    test_poi_stage_grid_constraint();
    test_poi_init_then_add_stage();
    test_poi_limit_too_many_stages();
    return 0;
}
