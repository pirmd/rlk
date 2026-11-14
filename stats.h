#ifndef RLK_STATS_H
#define RLK_STATS_H

/*
 * Deterministic descriptive stats over caller-owned rasters, header-only,
 * no allocation: histogram entropy, stream diversity (edge fraction, pair
 * entropy, patches), Moran's I, LZSS bits. Non-regression probes over a
 * generated map, or a headless quality report. rlk_stats_raster() bundles
 * the stream probes in one call.
 */

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Scratch buffer sizes for rlk_stats_lzss_bits. */
enum {
    RLK_STATS_LZ_WIN  = 4096,   /* window in bytes (power of two) */
    RLK_STATS_LZ_HEAD = 65536   /* 3-byte hash head table, in entries */
};

/* Shannon entropy of a histogram, in bits: -sum p*log2(p), zero counts
   skipped. total = sum of counts. */
static inline float
rlk_stats_entropy(const uint32_t *counts, int ntypes, int total) {
    assert(counts);
    assert(ntypes > 0 && total > 0);
    float h = 0.0f;
    for (int t = 0; t < ntypes; t++) {
        if (counts[t] == 0) continue;
        float p = (float)counts[t] / (float)total;
        h -= p * log2f(p);
    }
    return h;
}

/* Fraction of adjacent value pairs (right and down, 4-conn) whose values
   differ, over a w*h uint8_t stream in row-major order: spatial diversity.
   0 if the stream has fewer than 2 cells. */
static inline float
rlk_stats_edge_frac(const uint8_t *stream, int w, int h) {
    assert(stream);
    assert(w > 0 && h > 0);
    int edges = 0, pairs = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t t = stream[(size_t)y * (size_t)w + (size_t)x];
            if (x + 1 < w) {
                pairs++;
                if (stream[(size_t)y * (size_t)w + (size_t)(x + 1)] != t)
                    edges++;
            }
            if (y + 1 < h) {
                pairs++;
                if (stream[(size_t)(y + 1) * (size_t)w + (size_t)x] != t)
                    edges++;
            }
        }
    }
    return pairs > 0 ? (float)edges / (float)pairs : 0.0f;
}

/* Shannon entropy of adjacent value pairs (right and down), in bits per
   pair: captures spatial structure that rlk_stats_entropy misses.
   pair_count is a caller-owned scratch of ntypes*ntypes ints, zeroed by
   this call. Every stream value must be < ntypes (asserted). 0 if the
   stream has fewer than 2 cells. */
static inline float
rlk_stats_pair_entropy(const uint8_t *stream, int w, int h, int ntypes,
                       int *pair_count) {
    assert(stream && pair_count);
    assert(w > 0 && h > 0 && ntypes > 0);
    memset(pair_count, 0, (size_t)ntypes * (size_t)ntypes * sizeof(int));
    int pairs = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int t = stream[(size_t)y * (size_t)w + (size_t)x];
            assert(t < ntypes);
            if (x + 1 < w) {
                int t2 = stream[(size_t)y * (size_t)w + (size_t)(x + 1)];
                assert(t2 < ntypes);
                pair_count[t * ntypes + t2]++;
                pairs++;
            }
            if (y + 1 < h) {
                int t2 = stream[(size_t)(y + 1) * (size_t)w + (size_t)x];
                assert(t2 < ntypes);
                pair_count[t * ntypes + t2]++;
                pairs++;
            }
        }
    }
    if (pairs == 0) return 0.0f;
    float hp = 0.0f;
    for (int k = 0; k < ntypes * ntypes; k++) {
        if (pair_count[k] == 0) continue;
        float p = (float)pair_count[k] / (float)pairs;
        hp -= p * log2f(p);
    }
    return hp;
}

/* Moran's I of a w*h float grid, 4-conn (right/down pairs): positive =
   clumped, ~0 = random, negative = dispersed. I = (n/pairs)*(s1/s0) with
   s0 = sum (xi-mean)^2 and s1 = sum (xi-mean)(xj-mean) over pairs.
   Returns 0 if the grid is constant (s0 == 0) or has fewer than 2 cells. */
static inline float
rlk_stats_moran_i(const float *values, int w, int h) {
    assert(values);
    assert(w > 0 && h > 0);
    const int n = w * h;
    float mean = 0.0f;
    for (int i = 0; i < n; i++)
        mean += values[i];
    mean /= (float)n;
    int pairs = 0;
    float s0 = 0.0f, s1 = 0.0f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float xi = values[(size_t)y * (size_t)w + (size_t)x] - mean;
            s0 += xi * xi;
            if (x + 1 < w) {
                s1 += xi * (values[(size_t)y * (size_t)w + (size_t)(x + 1)] - mean);
                pairs++;
            }
            if (y + 1 < h) {
                s1 += xi * (values[(size_t)(y + 1) * (size_t)w + (size_t)x] - mean);
                pairs++;
            }
        }
    }
    return (s0 > 0.0f && pairs > 0)
               ? ((float)n / (float)pairs) * (s1 / s0)
               : 0.0f;
}

/* LZSS compressed size in bits of a byte stream (window RLK_STATS_LZ_WIN,
   match length 3..18; literal token 9 bits, match token 17 bits). Only the
   bit count is computed, no output buffer. head (RLK_STATS_LZ_HEAD ints)
   and prev (RLK_STATS_LZ_WIN ints) are caller-owned scratch, reset to -1 by
   this call. Deterministic.
 *
 *   int head[RLK_STATS_LZ_HEAD], prev[RLK_STATS_LZ_WIN];
 *   long bits = rlk_stats_lzss_bits(stream, n, head, prev);
 */
static inline long
rlk_stats_lzss_bits(const uint8_t *d, int n, int *head, int *prev) {
    assert(d && head && prev);
    assert(n >= 0);
    for (int k = 0; k < RLK_STATS_LZ_HEAD; k++)
        head[k] = -1;
    for (int k = 0; k < RLK_STATS_LZ_WIN; k++)
        prev[k] = -1;
    long bits = 0;
    int i = 0;
    while (i < n) {
        int best = 0;
        if (i + 3 <= n) {
            unsigned h = (((unsigned)d[i] << 10) ^ ((unsigned)d[i + 1] << 5)
                          ^ (unsigned)d[i + 2]) & 0xFFFFu;
            int j = head[h];
            int chain = 32;
            while (j >= 0 && i - j <= RLK_STATS_LZ_WIN && chain-- > 0) {
                int len = 0;
                while (len < 18 && i + len < n && d[j + len] == d[i + len])
                    len++;
                if (len > best)
                    best = len;
                j = prev[j & (RLK_STATS_LZ_WIN - 1)];
            }
            prev[i & (RLK_STATS_LZ_WIN - 1)] = head[h];
            head[h] = i;
        }
        if (best >= 3) {
            bits += 17;
            i += best;
        } else {
            bits += 9;
            i++;
        }
    }
    return bits;
}

/* Number of 4-connected same-value patches in a w*h uint8_t stream
   (row-major). seen (w*h uint8_t) and stack (w*h ints) are caller-owned
   scratch; seen is zeroed by this call.
 *
 *   uint8_t seen[W*H];
 *   int stack[W*H];
 *   int np = rlk_stats_patches(stream, W, H, seen, stack);
 */
static inline int
rlk_stats_patches(const uint8_t *stream, int w, int h, uint8_t *seen,
                  int *stack) {
    assert(stream && seen && stack);
    assert(w > 0 && h > 0);
    static const int d4[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    const int n = w * h;
    memset(seen, 0, (size_t)n);
    int np = 0;
    for (int i = 0; i < n; i++) {
        if (seen[i])
            continue;
        uint8_t t = stream[i];
        int top = 0;
        stack[top++] = i;
        seen[i] = 1;
        while (top > 0) {
            int c = stack[--top];
            int cx = c % w, cy = c / w;
            for (int k = 0; k < 4; k++) {
                int nx = cx + d4[k][0], ny = cy + d4[k][1];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                    continue;
                int ni = ny * w + nx;
                if (seen[ni] || stream[ni] != t)
                    continue;
                seen[ni] = 1;
                stack[top++] = ni;
            }
        }
        np++;
    }
    return np;
}

/* Workspace element type for rlk_stats_raster: a union so the blob is
   max-aligned; size the array with RLK_STATS_WS_UNITS (a constant
   expression), e.g.
       static rlk_stats_ws_unit_t ws[RLK_STATS_WS_UNITS(128, 96, TILE_COUNT)];
*/
typedef union {
    int i;
    uint32_t u;
    float f;
    double d;
    long l;
    void *p;
} rlk_stats_ws_unit_t;

/* Number of rlk_stats_ws_unit_t elements needed by rlk_stats_raster for a
   w*h raster of ntypes values. Byte layout (internal): counts (ntypes
   uint32_t) | pair_count (ntypes^2 int) | stack (w*h int) | lz head/prev
   | seen (w*h bytes, last: only byte alignment needed). */
#define RLK_STATS_WS_UNITS(w, h, ntypes) \
    (((size_t)(ntypes) * sizeof(uint32_t) \
      + (size_t)(ntypes) * (size_t)(ntypes) * sizeof(int) \
      + (size_t)(w) * (size_t)(h) * sizeof(int) \
      + (size_t)(RLK_STATS_LZ_HEAD + RLK_STATS_LZ_WIN) * sizeof(int) \
      + (size_t)(w) * (size_t)(h) \
      + sizeof(rlk_stats_ws_unit_t) - 1) / sizeof(rlk_stats_ws_unit_t))

/* Result of rlk_stats_raster: the stream probes of the kernels above. */
typedef struct {
    float entropy;         /* Shannon entropy of the type histogram, bits */
    float edge_frac;       /* differing 4-conn adjacent pairs, fraction */
    float pair_entropy;    /* Shannon entropy of adjacent pairs, bits/pair */
    int   n_patches;       /* 4-connected same-type patches */
    long  lzss_bits;       /* LZSS compressed size of the stream, bits */
    float compress_ratio;  /* lzss_bits / (8 * w * h); low = repetitive */
} rlk_stats_raster_t;

/* One-shot probes over a w*h uint8_t type raster (row-major, every value
   < ntypes). Fills *out; ws must point to RLK_STATS_WS_UNITS(w, h, ntypes)
   rlk_stats_ws_unit_t elements (layout internal, reset by this call).
   Results are bit-identical to calling the kernels individually.
   Deterministic. */
static inline void
rlk_stats_raster(const uint8_t *s, int w, int h, int ntypes,
                 rlk_stats_raster_t *out, rlk_stats_ws_unit_t *ws) {
    assert(s && out && ws);
    assert(w > 0 && h > 0 && ntypes > 0);
    const size_t n = (size_t)w * (size_t)h;
    assert(n <= (size_t)INT_MAX);
    uint32_t *counts = (uint32_t *)(void *)ws;
    int *pair_count = (int *)(counts + ntypes);
    int *stack = pair_count + (size_t)ntypes * (size_t)ntypes;
    int *lz_head = stack + n;
    int *lz_prev = lz_head + RLK_STATS_LZ_HEAD;
    uint8_t *seen = (uint8_t *)(lz_prev + RLK_STATS_LZ_WIN);

    memset(counts, 0, (size_t)ntypes * sizeof(uint32_t));
    int total = 0;
    for (size_t i = 0; i < n; i++) {
        assert(s[i] < ntypes);
        counts[s[i]]++;
        total++;
    }
    out->entropy = rlk_stats_entropy(counts, ntypes, total);
    out->edge_frac = rlk_stats_edge_frac(s, w, h);
    out->pair_entropy = rlk_stats_pair_entropy(s, w, h, ntypes, pair_count);
    out->n_patches = rlk_stats_patches(s, w, h, seen, stack);
    out->lzss_bits = rlk_stats_lzss_bits(s, (int)n, lz_head, lz_prev);
    out->compress_ratio = (float)out->lzss_bits / (8.0f * (float)n);
}

#endif /* RLK_STATS_H */
