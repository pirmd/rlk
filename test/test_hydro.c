#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "hydro.h"

/* Fixture A: real drain on an 8x8 grid, cellsize 4, elevation decreasing
   eastward (elev = FW-1-i): flow is eastward, east edge is the outlet. */

#define FW 8
#define FH 8
#define FCELL 4

static float elev_buf[FW * FH];
static uint16_t filled_buf[FW * FH];
static uint8_t dir_buf[FW * FH];
static uint32_t acc_buf[FW * FH];
static int order_buf[FW * FH];
static uint64_t drain_scratch[FW * FH];

static rlk_grid_t elev;
static rlk_drain_t drain;

static float slope_buf[FW * FH];
static uint8_t strahler_buf[FW * FH];
static float speed_buf[FW * FH];
static uint16_t hydro_scratch[FW * FH];

static void
fixture_slope(void) {
    elev = (rlk_grid_t){ .lattice = { .w = FW, .h = FH, .cellsize = FCELL,
                                       .origin_x = 0, .origin_y = 0 }, .values = elev_buf };
    for (int j = 0; j < FH; j++)
        for (int i = 0; i < FW; i++)
            elev.values[(size_t)j * FW + (size_t)i] = (float)(FW - 1 - i);
    drain = (rlk_drain_t){ .lattice = elev.lattice, .filled = filled_buf,
                           .dir = dir_buf, .acc = acc_buf, .order = order_buf };
    assert(rlk_drain_build(&drain, &elev, -1.0f, drain_scratch) == RLK_OK);
}

/* Hand-built linear drain: 4x1 cells of 4 tiles, all flowing east.
   filled 1024/768/512/0 -> one quantization step (1.0) per cell -> slope 0.25. */
#define LW 4
#define LH 1

static uint16_t l_filled[LW * LH];
static uint8_t l_dir[LW * LH];
static uint32_t l_acc[LW * LH];
static int l_order[LW * LH];
static uint16_t l_scratch[LW * LH];
static float l_slope[LW * LH];
static float l_speed[LW * LH];

static rlk_drain_t
fixture_linear(void) {
    l_filled[0] = 768; l_filled[1] = 512; l_filled[2] = 256; l_filled[3] = 0;
    l_dir[0] = 0; l_dir[1] = 0; l_dir[2] = 0; l_dir[3] = RLK_DRAIN_OUTLET;
    l_acc[0] = 1; l_acc[1] = 2; l_acc[2] = 3; l_acc[3] = 4;
    l_order[0] = 3; l_order[1] = 2; l_order[2] = 1; l_order[3] = 0;
    return (rlk_drain_t){ .lattice = { .w = LW, .h = LH, .cellsize = FCELL,
                                       .origin_x = 0, .origin_y = 0 },
                          .filled = l_filled, .dir = l_dir,
                          .acc = l_acc, .order = l_order };
}

/* Slope: 256/256 quantization steps over 4 tiles -> 0.25; 0 at the outlet. */
static void
test_hydro_slope(void) {
    rlk_drain_t d = fixture_linear();
    rlk_hydro_t h = { .lattice = d.lattice, .slope = l_slope,
                      .strahler = NULL, .speed = NULL };
    rlk_hydro_build(&h, &d, 1u, l_scratch);

    for (int i = 0; i < LW - 1; i++)
        assert(fabsf(l_slope[i] - 0.25f) < 1.0e-6f);
    assert(l_slope[LW - 1] == 0.0f);  /* outlet */
}

/* Speed: positive on slopes, 0 at the outlet, grows downstream (wider acc). */
static void
test_hydro_speed(void) {
    rlk_drain_t d = fixture_linear();
    rlk_hydro_t h = { .lattice = d.lattice, .slope = NULL,
                      .strahler = NULL, .speed = l_speed };
    rlk_hydro_build(&h, &d, 1u, l_scratch);

    for (int i = 0; i < LW - 1; i++)
        assert(l_speed[i] > 0.0f);
    assert(l_speed[LW - 1] == 0.0f);
    assert(l_speed[2] > l_speed[1] && l_speed[1] > l_speed[0]);
}

/* Speed with acc_min above headwater acc: width clamped low at 0.25, speed
   stays > 0 but smaller than with acc_min = 1. */
static void
test_hydro_speed_acc_min_clamp(void) {
    rlk_drain_t d = fixture_linear();
    rlk_hydro_t h = { .lattice = d.lattice, .slope = NULL,
                      .strahler = NULL, .speed = l_speed };
    rlk_hydro_build(&h, &d, 4u, l_scratch);  /* acc 1..4, acc_min 4 */
    const float clamped = l_speed[0];
    assert(clamped > 0.0f);  /* headwater clamped, not zero/negative */
    rlk_hydro_build(&h, &d, 1u, l_scratch);
    assert(l_speed[0] > clamped);  /* wider law without the clamp */
}

/* Real drain on a slope: smoke test — slope/speed >= 0, 0 at outlets,
   positive in the interior. */
static void
test_hydro_real_drain_signs(void) {
    fixture_slope();
    rlk_hydro_t h = { .lattice = drain.lattice, .slope = slope_buf,
                      .strahler = NULL, .speed = speed_buf };
    rlk_hydro_build(&h, &drain, 1u, hydro_scratch);

    for (int c = 0; c < FW * FH; c++) {
        assert(slope_buf[c] >= 0.0f);
        assert(speed_buf[c] >= 0.0f);
        if (drain.dir[c] == RLK_DRAIN_OUTLET) {
            assert(slope_buf[c] == 0.0f);
            assert(speed_buf[c] == 0.0f);
        }
    }
    /* at least one interior cell has a positive slope */
    int any = 0;
    for (int c = 0; c < FW * FH && !any; c++)
        if (slope_buf[c] > 0.0f) any = 1;
    assert(any);
}

/* Strahler on a linear grid: every cell has a single upstream -> all 1. */
static void
test_hydro_strahler_linear(void) {
    fixture_slope();
    rlk_hydro_t h = { .lattice = drain.lattice, .slope = NULL,
                      .strahler = strahler_buf, .speed = NULL };
    rlk_hydro_build(&h, &drain, 1u, hydro_scratch);
    for (int c = 0; c < FW * FH; c++)
        assert(strahler_buf[c] == 1);
}

/* Fixture B: hand-built Y network, 3x2, cellsize 1.
     (0,0) -> (1,0) --SE--> (2,1) OUTLET
     (2,0) --S--> (2,1)      ^
     (0,1) -> (1,1) --E-----+                       */
#define YW 3
#define YH 2

static uint16_t y_filled[YW * YH];
static uint8_t y_dir[YW * YH];
static uint32_t y_acc[YW * YH];
static int y_order[YW * YH];
static uint16_t y_scratch[YW * YH];
static float y_slope[YW * YH];
static uint8_t y_strahler[YW * YH];
static float y_speed[YW * YH];

static rlk_drain_t
fixture_y(void) {
    /* cells: 0=(0,0) 1=(1,0) 2=(2,0) 3=(0,1) 4=(1,1) 5=(2,1) */
    y_filled[0] = 512; y_filled[1] = 256; y_filled[2] = 256;
    y_filled[3] = 512; y_filled[4] = 256; y_filled[5] = 0;
    y_dir[0] = 0;   /* E  -> 1 */
    y_dir[1] = 1;   /* SE -> 5 */
    y_dir[2] = 2;   /* S  -> 5 */
    y_dir[3] = 0;   /* E  -> 4 */
    y_dir[4] = 0;   /* E  -> 5 */
    y_dir[5] = RLK_DRAIN_OUTLET;
    y_acc[0] = 1; y_acc[1] = 2; y_acc[2] = 1;
    y_acc[3] = 1; y_acc[4] = 2; y_acc[5] = 6;
    /* downstream before upstream */
    int o[YW * YH] = { 5, 1, 2, 4, 0, 3 };
    for (int c = 0; c < YW * YH; c++)
        y_order[c] = o[c];
    return (rlk_drain_t){ .lattice = { .w = YW, .h = YH, .cellsize = 1,
                                       .origin_x = 0, .origin_y = 0 },
                          .filled = y_filled, .dir = y_dir,
                          .acc = y_acc, .order = y_order };
}

/* Confluence at (2,1): three order-1 upstreams -> Strahler 2. Sources stay 1. */
static void
test_hydro_strahler_confluence(void) {
    rlk_drain_t d = fixture_y();
    rlk_hydro_t h = { .lattice = d.lattice, .slope = y_slope,
                      .strahler = y_strahler, .speed = y_speed };
    rlk_hydro_build(&h, &d, 1u, y_scratch);

    assert(y_strahler[0] == 1);
    assert(y_strahler[1] == 1);
    assert(y_strahler[2] == 1);
    assert(y_strahler[3] == 1);
    assert(y_strahler[4] == 1);
    assert(y_strahler[5] == 2);   /* confluence of three order-1 streams */

    /* slope: positive downhill, 0 at the outlet */
    assert(y_slope[5] == 0.0f);
    assert(y_slope[2] > 0.0f);            /* S, 1 tile: 256/256 = 1.0 */
    assert(fabsf(y_slope[2] - 1.0f) < 1.0e-6f);
    assert(y_slope[1] > 0.0f);            /* SE, diagonal */
    assert(y_speed[5] == 0.0f);
    assert(y_speed[2] > 0.0f);
}

/* NULL buffers: each output optional, any combination must work. */
static void
test_hydro_null_buffers(void) {
    fixture_slope();
    rlk_hydro_t h = { .lattice = drain.lattice, .slope = NULL,
                      .strahler = NULL, .speed = NULL };
    h.slope = slope_buf;
    rlk_hydro_build(&h, &drain, 1u, hydro_scratch);
    h.slope = NULL; h.strahler = strahler_buf;
    rlk_hydro_build(&h, &drain, 1u, hydro_scratch);
    h.strahler = NULL; h.speed = speed_buf;
    rlk_hydro_build(&h, &drain, 1u, hydro_scratch);
    assert(speed_buf[2 * FW + 2] > 0.0f);  /* interior cell */
}

/* Main */

int
main(void) {
    test_hydro_slope();
    test_hydro_speed();
    test_hydro_speed_acc_min_clamp();
    test_hydro_real_drain_signs();
    test_hydro_strahler_linear();
    test_hydro_strahler_confluence();
    test_hydro_null_buffers();
    return 0;
}
