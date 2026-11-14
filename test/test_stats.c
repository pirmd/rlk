#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "stats.h"

/* entropy: histogram {1,1,2} over 4 cells -> 1.5 bits; one class -> 0. */

static void
test_entropy(void) {
    uint32_t counts[3] = { 1, 1, 2 };
    assert(fabsf(rlk_stats_entropy(counts, 3, 4) - 1.5f) < 1e-6f);
    uint32_t one_class[2] = { 0, 5 };
    assert(rlk_stats_entropy(one_class, 2, 5) == 0.0f);
}

/* edge_frac / pair_entropy on a 3x2 stream (rows: 1 2 1 / 1 1 2):
   7 adjacent pairs, 5 differing; pair counts (1,2)=3, (2,1)=2, (1,1)=2. */

static const uint8_t S32[6] = { 1, 2, 1, 1, 1, 2 };

static void
test_edge_pair(void) {
    assert(fabsf(rlk_stats_edge_frac(S32, 3, 2) - 5.0f / 7.0f) < 1e-6f);
    int pair_count[9];
    memset(pair_count, 0xAA, sizeof(pair_count));  /* must be reset by the call */
    float hp = rlk_stats_pair_entropy(S32, 3, 2, 3, pair_count);
    assert(fabsf(hp - 1.5566567f) < 1e-4f);
    int sum = 0;
    for (int k = 0; k < 9; k++)
        sum += pair_count[k];
    assert(sum == 7);
    /* 1x1 stream: no pairs. */
    uint8_t one[1] = { 0 };
    assert(rlk_stats_edge_frac(one, 1, 1) == 0.0f);
    memset(pair_count, 0xAA, sizeof(pair_count));
    assert(rlk_stats_pair_entropy(one, 1, 1, 1, pair_count) == 0.0f);
}

/* moran_i: 2x2 checkerboard -> -1, constant grid -> 0, {0,0,0,1} -> -1/3. */

static void
test_moran(void) {
    float checker[4] = { 0, 1, 1, 0 };
    assert(rlk_stats_moran_i(checker, 2, 2) == -1.0f);
    float flat[4] = { 2, 2, 2, 2 };
    assert(rlk_stats_moran_i(flat, 2, 2) == 0.0f);
    float clump[4] = { 0, 0, 0, 1 };
    assert(fabsf(rlk_stats_moran_i(clump, 2, 2) - (-1.0f / 3.0f)) < 1e-6f);
    float one[1] = { 5 };
    assert(rlk_stats_moran_i(one, 1, 1) == 0.0f);
}

/* lzss_bits: 20 identical bytes -> literal + 18-match + literal = 35 bits;
   2 bytes -> 2 literals = 18; empty -> 0. Scratch must be reset by the call. */

static void
test_lzss(void) {
    uint8_t rep[20];
    memset(rep, 7, sizeof(rep));
    int head[RLK_STATS_LZ_HEAD], prev[RLK_STATS_LZ_WIN];
    memset(head, 0xAA, sizeof(head));
    memset(prev, 0xAA, sizeof(prev));
    assert(rlk_stats_lzss_bits(rep, 20, head, prev) == 35);
    uint8_t two[2] = { 'a', 'b' };
    assert(rlk_stats_lzss_bits(two, 2, head, prev) == 18);
    assert(rlk_stats_lzss_bits(two, 0, head, prev) == 0);
}

/* patches: 3x3 stream below has 3 same-value 4-conn patches (1s, 2s, 3s). */

static const uint8_t S33[9] = { 1, 1, 2,
                                1, 2, 2,
                                3, 3, 2 };

static void
test_patches(void) {
    uint8_t seen[9];
    int stack[9];
    memset(seen, 1, sizeof(seen));  /* must be reset by the call */
    assert(rlk_stats_patches(S33, 3, 3, seen, stack) == 3);
    for (int i = 0; i < 9; i++)
        assert(seen[i] == 1);
}

/* Example from the rlk_stats_lzss_bits banner. */

static void
test_example_rlk_stats_lzss_bits(void) {
    uint8_t stream[20];
    memset(stream, 7, sizeof(stream));
    int head[RLK_STATS_LZ_HEAD], prev[RLK_STATS_LZ_WIN];
    long bits = rlk_stats_lzss_bits(stream, 20, head, prev);
    assert(bits == 35);
}

/* Example from the rlk_stats_patches banner. */

static void
test_example_rlk_stats_patches(void) {
    uint8_t seen[9];
    int stack[9];
    int np = rlk_stats_patches(S33, 3, 3, seen, stack);
    assert(np == 3);
}

/* rlk_stats_raster bundles the stream kernels over one workspace: results
   must be bit-identical to calling the kernels individually, and the
   workspace of exactly RLK_STATS_WS_UNITS elements must suffice (canaries
   around it stay untouched, prefill must be reset by the call). */

static const uint8_t S43[12] = { 0, 1, 0, 2,
                                 1, 1, 2, 0,
                                 2, 0, 1, 1 };

static void
test_raster_bundle(void) {
    const int w = 4, h = 3, nt = 3;
    const int n = w * h;
    uint32_t counts[3];
    memset(counts, 0, sizeof(counts));
    int total = 0;
    for (int i = 0; i < n; i++) {
        counts[S43[i]]++;
        total++;
    }
    int pc[9];
    uint8_t seen[12];
    int st[12];
    int head[RLK_STATS_LZ_HEAD], prev[RLK_STATS_LZ_WIN];
    float e = rlk_stats_entropy(counts, nt, total);
    float ef = rlk_stats_edge_frac(S43, w, h);
    float pe = rlk_stats_pair_entropy(S43, w, h, nt, pc);
    int np = rlk_stats_patches(S43, w, h, seen, st);
    long bits = rlk_stats_lzss_bits(S43, n, head, prev);

    static rlk_stats_ws_unit_t canary_lo[8];
    static rlk_stats_ws_unit_t ws[RLK_STATS_WS_UNITS(4, 3, 3)];
    static rlk_stats_ws_unit_t canary_hi[8];
    memset(canary_lo, 0x5A, sizeof(canary_lo));
    memset(canary_hi, 0xA5, sizeof(canary_hi));
    memset(ws, 0xAA, sizeof(ws));
    rlk_stats_raster_t r;
    rlk_stats_raster(S43, w, h, nt, &r, ws);
    assert(r.entropy == e);
    assert(r.edge_frac == ef);
    assert(r.pair_entropy == pe);
    assert(r.n_patches == np);
    assert(r.lzss_bits == bits);
    assert(r.compress_ratio == (float)bits / (8.0f * (float)n));
    for (int i = 0; i < 8; i++) {
        assert(canary_lo[i].u == 0x5A5A5A5Au);
        assert(canary_hi[i].u == 0xA5A5A5A5u);
    }
}

/* Main */

int
main(void) {
    test_entropy();
    test_edge_pair();
    test_moran();
    test_lzss();
    test_patches();
    test_example_rlk_stats_lzss_bits();
    test_example_rlk_stats_patches();
    test_raster_bundle();
    return 0;
}
