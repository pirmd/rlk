#include "test.h"
#include "rl/world/tilemap.h"
#include "rl/world/view.h"

/* Fixture: a 10x8 tilemap with a distinct tile type per cell. */
#define FX_W 10
#define FX_H 8
static unsigned char fx_buf[RLK_TM_BUF_SIZE(FX_W, FX_H)];
static rlk_tilemap_t fx_tm;

static void
fixture_setup(void) {
    ASSERT(rlk_tilemap_init(&fx_tm, FX_W, FX_H, 1.0f, fx_buf, sizeof fx_buf) == RLK_OK);
    for (int y = 0; y < FX_H; y++) {
        for (int x = 0; x < FX_W; x++) {
            rlk_tilemap_get(&fx_tm, x, y)->type = (uint8_t)(x + y * FX_W + 1);
        }
    }
}

/* --- rlk_view_init ------------------------------------------------------- */
static void
test_view_init(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_tm, 0, 0, 4, 4) == RLK_OK);
    ASSERT(v.tm == &fx_tm);
    ASSERT(v.ox == 0 && v.oy == 0 && v.vw == 4 && v.vh == 4);
}

static void
test_view_init_invalid(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, NULL, 0, 0, 4, 4) == RLK_E_CONFIG);
    ASSERT(rlk_view_init(&v, &fx_tm, 0, 0, 0, 4) == RLK_E_CONFIG);
    ASSERT(rlk_view_init(&v, &fx_tm, 0, 0, 4, 0) == RLK_E_CONFIG);
}

static void
test_view_init_clamps_origin(void) {
    fixture_setup();
    rlk_view_t v;
    /* origin overhanging bottom-right is pulled back inside */
    ASSERT(rlk_view_init(&v, &fx_tm, 100, 100, 4, 4) == RLK_OK);
    ASSERT(v.ox == FX_W - 4);
    ASSERT(v.oy == FX_H - 4);
    /* negative origin clamped to 0 */
    ASSERT(rlk_view_init(&v, &fx_tm, -5, -5, 4, 4) == RLK_OK);
    ASSERT(v.ox == 0 && v.oy == 0);
}

static void
test_view_init_oversized_shrinks(void) {
    fixture_setup();
    rlk_view_t v;
    /* view larger than tilemap is shrunk to the tilemap */
    ASSERT(rlk_view_init(&v, &fx_tm, 0, 0, 100, 100) == RLK_OK);
    ASSERT(v.vw == FX_W && v.vh == FX_H);
}

/* --- rlk_view_map / rlk_view_get ---------------------------------------- */
static void
test_view_map(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_tm, 2, 3, 4, 4) == RLK_OK);
    int gx, gy;
    ASSERT(rlk_view_map(&v, 0, 0, &gx, &gy));
    ASSERT(gx == 2 && gy == 3);
    ASSERT(rlk_view_map(&v, 3, 3, &gx, &gy));
    ASSERT(gx == 5 && gy == 6);
    /* view cell maps to a valid tilemap cell, so view_get returns that cell */
    ASSERT(rlk_view_get(&v, 0, 0)->type == (uint8_t)(2 + 3 * FX_W + 1));
    ASSERT(rlk_view_get(&v, 3, 3)->type == (uint8_t)(5 + 6 * FX_W + 1));
}

static void
test_view_get_out_of_view(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_tm, 0, 0, 4, 4) == RLK_OK);
    ASSERT(rlk_view_get(&v, -1, 0) == NULL);
    ASSERT(rlk_view_get(&v, 4, 0) == NULL);
    /* out-of-view solidity => solid */
    ASSERT(rlk_view_is_solid(&v, 4, 0) == 1);
}

/* --- rlk_view_center ---------------------------------------------------- */
static void
test_view_center_clamped(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_tm, 0, 0, 4, 4) == RLK_OK);
    /* center near top-left clamps to origin (0,0) */
    rlk_view_center(&v, 0, 0);
    ASSERT(v.ox == 0 && v.oy == 0);
    /* center near bottom-right clamps to max origin */
    rlk_view_center(&v, FX_W - 1, FX_H - 1);
    ASSERT(v.ox == FX_W - 4 && v.oy == FX_H - 4);
    /* center in the middle maps the middle cell through view_get */
    rlk_view_center(&v, 5, 4);
    ASSERT(rlk_view_get(&v, v.vw / 2, v.vh / 2)->type == (uint8_t)(5 + 4 * FX_W + 1));
}

/* --- rlk_view_move ------------------------------------------------------ */
static void
test_view_move_clamped(void) {
    fixture_setup();
    rlk_view_t v;
    ASSERT(rlk_view_init(&v, &fx_tm, 0, 0, 4, 4) == RLK_OK);
    rlk_view_move(&v, 1, 1);
    ASSERT(v.ox == 1 && v.oy == 1);
    /* move past bottom-right clamps to max origin */
    rlk_view_move(&v, 100, 100);
    ASSERT(v.ox == FX_W - 4 && v.oy == FX_H - 4);
    /* move past top-left clamps to 0 */
    rlk_view_move(&v, -100, -100);
    ASSERT(v.ox == 0 && v.oy == 0);
}

int
main(void) {
    test_view_init();
    test_view_init_invalid();
    test_view_init_clamps_origin();
    test_view_init_oversized_shrinks();
    test_view_map();
    test_view_get_out_of_view();
    test_view_center_clamped();
    test_view_move_clamped();
    return 0;
}
