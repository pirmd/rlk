#include <assert.h>
#include <stdint.h>

#include "scatter.h"

/* Fixture: 8x6 cells of 2 tiles, origin (10,20). */

#define MW 8
#define MH 6
#define CS 2

static rlk_lattice_t
fixture_lat(void) {
    return (rlk_lattice_t){ .w = MW, .h = MH, .cellsize = CS,
                            .origin_x = 10, .origin_y = 20 };
}

/* at: point stays inside its cell, world coords. */

static void
test_at_bounds(void) {
    rlk_lattice_t lat = fixture_lat();
    for (int j = 0; j < MH; j++) {
        for (int i = 0; i < MW; i++) {
            rlk_scatter_point_t p;
            rlk_scatter_at(&p, lat, 42, 7, i, j);
            float x = (float)(lat.origin_x + i * CS);
            float y = (float)(lat.origin_y + j * CS);
            assert(p.px >= x && p.px < x + CS);
            assert(p.py >= y && p.py < y + CS);
        }
    }
}

/* at: deterministic per (seed, salt); a different salt decorrelates. */

static void
test_at_deterministic(void) {
    rlk_lattice_t lat = fixture_lat();
    rlk_scatter_point_t a, b;
    rlk_scatter_at(&a, lat, 42, 7, 3, 4);
    rlk_scatter_at(&b, lat, 42, 7, 3, 4);
    assert(a.px == b.px && a.py == b.py);

    int differ = 0;
    for (int j = 0; j < MH; j++)
        for (int i = 0; i < MW; i++) {
            rlk_scatter_point_t c;
            rlk_scatter_at(&c, lat, 42, 8, i, j);
            rlk_scatter_at(&b, lat, 42, 7, i, j);
            if (c.px != b.px || c.py != b.py)
                differ = 1;
        }
    assert(differ);
}

/* mask: full-weight mask -> one point per cell, row-major, each point in
   its own cell. */

static void
test_mask_full(void) {
    rlk_lattice_t lat = fixture_lat();
    uint8_t w[MW * MH];
    for (int k = 0; k < MW * MH; k++)
        w[k] = 255;
    rlk_mask_t m = { .lattice = lat, .weights = w };

    rlk_scatter_point_t out[MW * MH];
    int n = rlk_scatter_mask(out, MW * MH, lat, &m, 42, 7);
    assert(n == MW * MH);
    for (int k = 0; k < n; k++) {
        int idx = rlk_lattice_index(&lat, (int)out[k].px, (int)out[k].py);
        assert(idx == k);
    }
}

/* mask: weight 0 cells are skipped; all-zero mask -> 0 points. */

static void
test_mask_zero_weight(void) {
    rlk_lattice_t lat = fixture_lat();
    uint8_t w[MW * MH];
    for (int k = 0; k < MW * MH; k++)
        w[k] = 0;
    rlk_mask_t m = { .lattice = lat, .weights = w };
    rlk_scatter_point_t out[MW * MH];
    assert(rlk_scatter_mask(out, MW * MH, lat, &m, 42, 7) == 0);

    w[3 * MW + 5] = 255;  /* cell (5,3) only */
    int n = rlk_scatter_mask(out, MW * MH, lat, &m, 42, 7);
    assert(n == 1);
    assert(rlk_lattice_index(&lat, (int)out[0].px, (int)out[0].py) == 3 * MW + 5);
}

/* mask: weight w keeps ~w/255 of the cells (64x64, w=128 -> ~2048). */

static void
test_mask_density(void) {
    enum { W = 64, H = 64 };
    static uint8_t w[W * H];
    for (int k = 0; k < W * H; k++)
        w[k] = 128;
    rlk_lattice_t lat = { .w = W, .h = H, .cellsize = 1,
                          .origin_x = 0, .origin_y = 0 };
    rlk_mask_t m = { .lattice = lat, .weights = w };
    static rlk_scatter_point_t out[W * H];
    int n = rlk_scatter_mask(out, W * H, lat, &m, 42, 7);
    assert(n > W * H * 3 / 10 && n < W * H * 7 / 10);
}

/* mask: cap truncates deterministically (same prefix as the full run). */

static void
test_mask_cap(void) {
    rlk_lattice_t lat = fixture_lat();
    uint8_t w[MW * MH];
    for (int k = 0; k < MW * MH; k++)
        w[k] = 255;
    rlk_mask_t m = { .lattice = lat, .weights = w };

    rlk_scatter_point_t full[MW * MH], part[MW * MH];
    int nf = rlk_scatter_mask(full, MW * MH, lat, &m, 42, 7);
    int np = rlk_scatter_mask(part, 5, lat, &m, 42, 7);
    assert(np == 5);
    for (int k = 0; k < np; k++)
        assert(part[k].px == full[k].px && part[k].py == full[k].py);
    assert(nf == MW * MH);
}

/* Banner example of rlk_scatter_at. */

static void
test_example_rlk_scatter_at(void) {
    rlk_lattice_t lat = fixture_lat();
    rlk_scatter_point_t p;
    rlk_scatter_at(&p, lat, 42, 7, 0, 0);
    assert(p.px >= 10.0f && p.px < 12.0f);
    assert(p.py >= 20.0f && p.py < 22.0f);
}

int
main(void) {
    test_at_bounds();
    test_at_deterministic();
    test_mask_full();
    test_mask_zero_weight();
    test_mask_density();
    test_mask_cap();
    test_example_rlk_scatter_at();
    return 0;
}
