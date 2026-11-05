/*
 * gen_stats - post-generation map probes for the tilemap_gen demo
 * (see tilemap_gen.h): tile-type histogram + Shannon entropy, water/river/
 * land fractions, connected walkable components (flood fill), POI counts.
 * Deterministic per seed: usable as a non-regression harness.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gen_internal.h"

/* Walkable land for connectivity: same set as the road mask of demo_world
   (no water, rock, house or void). */
int
tilemap_gen_walkable(uint8_t type) {
    switch (type) {
    case TILE_GRASS: case TILE_SAND: case TILE_TREE: case TILE_SNOW:
    case TILE_MARSH: case TILE_FLOOR: case TILE_PINE: case TILE_PLAZA:
    case TILE_DOCK: case TILE_SINKHOLE:
        return 1;
    default:
        return 0;
    }
}

/* LZSS compressed size in bits of the tile-type stream (window 4 KiB,
   match length 3..18; literal token 9 bits, match token 17 bits). Only the
   bit count is computed, no output buffer. Deterministic. */
#define LZ_WIN 4096
#define LZ_MIN 3
#define LZ_MAX 18

static long
lzss_bits(const uint8_t *d, int n) {
    static int head[65536];  /* hash of 3 bytes -> most recent position */
    static int prev[LZ_WIN]; /* hash chain, indexed by pos % LZ_WIN */
    for (int k = 0; k < 65536; k++)
        head[k] = -1;
    long bits = 0;
    int i = 0;
    while (i < n) {
        int best = 0;
        int has = i + LZ_MIN <= n;
        unsigned h = 0;
        if (has) {
            h = (((unsigned)d[i] << 10) ^ ((unsigned)d[i + 1] << 5)
                 ^ (unsigned)d[i + 2]) & 0xFFFFu;
            int j = head[h];
            int chain = 32;
            while (j >= 0 && i - j <= LZ_WIN && chain-- > 0) {
                int len = 0;
                while (len < LZ_MAX && i + len < n && d[j + len] == d[i + len])
                    len++;
                if (len > best)
                    best = len;
                j = prev[j & (LZ_WIN - 1)];
            }
            prev[i & (LZ_WIN - 1)] = head[h];
            head[h] = i;
        }
        if (best >= LZ_MIN) {
            bits += 17;
            i += best;
        } else {
            bits += 9;
            i++;
        }
    }
    return bits;
}

void
tilemap_gen_stats(const tilemap_gen_t *gen, tilemap_gen_stats_t *st,
                  uint8_t *scratch) {
    memset(st, 0, sizeof(*st));
    const int n = TM_W * TM_H;

    int n_land = 0, n_water = 0, n_river = 0;
    for (int i = 0; i < n; i++) {
        uint8_t t = rlk_tilemap_get(&gen->tm, i % TM_W, i / TM_W)->type;
        if (t < TILE_COUNT)
            st->tile_count[t]++;
        if (t == TILE_WATER || t == TILE_LAKE) n_water++;
        else if (t == TILE_RIVER) n_river++;
        if (tilemap_gen_walkable(t)) n_land++;
        if (t == TILE_SINKHOLE) st->n_sinkholes++;
    }
    st->water_frac = (float)n_water / (float)n;
    st->river_frac = (float)n_river / (float)n;
    st->land_frac  = (float)n_land / (float)n;

    /* Shannon entropy over tile types (bits) */
    float h = 0.0f;
    for (int t = 0; t < TILE_COUNT; t++) {
        if (st->tile_count[t] == 0) continue;
        float p = (float)st->tile_count[t] / (float)n;
        h -= p * log2f(p);
    }
    st->entropy = h;

    /* Spatial diversity: fraction of adjacent tile pairs (right/down,
       4-conn) whose types differ. */
    int edges = 0, pairs = 0;
    for (int y = 0; y < TM_H; y++) {
        for (int x = 0; x < TM_W; x++) {
            uint8_t t = rlk_tilemap_get(&gen->tm, x, y)->type;
            if (x + 1 < TM_W) {
                pairs++;
                if (rlk_tilemap_get(&gen->tm, x + 1, y)->type != t)
                    edges++;
            }
            if (y + 1 < TM_H) {
                pairs++;
                if (rlk_tilemap_get(&gen->tm, x, y + 1)->type != t)
                    edges++;
            }
        }
    }
    st->edge_frac = (float)edges / (float)pairs;

    /* Shannon entropy of adjacent tile pairs (right/down), bits per pair:
       captures spatial structure that the unigram entropy misses. */
    int pair_count[TILE_COUNT * TILE_COUNT];
    memset(pair_count, 0, sizeof(pair_count));
    for (int y = 0; y < TM_H; y++) {
        for (int x = 0; x < TM_W; x++) {
            int t = rlk_tilemap_get(&gen->tm, x, y)->type;
            if (x + 1 < TM_W)
                pair_count[t * TILE_COUNT + rlk_tilemap_get(&gen->tm, x + 1, y)->type]++;
            if (y + 1 < TM_H)
                pair_count[t * TILE_COUNT + rlk_tilemap_get(&gen->tm, x, y + 1)->type]++;
        }
    }
    float hp = 0.0f;
    for (int k = 0; k < TILE_COUNT * TILE_COUNT; k++) {
        if (pair_count[k] == 0) continue;
        float p = (float)pair_count[k] / (float)pairs;
        hp -= p * log2f(p);
    }
    st->pair_entropy = hp;

    /* Moran's I over the final elevation field (4-conn, right/down pairs):
       positive = clumped terrain, ~0 = random, negative = dispersed. */
    float mean = 0.0f;
    for (int i = 0; i < n; i++)
        mean += gen->final_elev[i];
    mean /= (float)n;
    float s0 = 0.0f, s1 = 0.0f;
    for (int y = 0; y < TM_H; y++) {
        for (int x = 0; x < TM_W; x++) {
            float xi = gen->final_elev[y * TM_W + x] - mean;
            s0 += xi * xi;
            if (x + 1 < TM_W)
                s1 += xi * (gen->final_elev[y * TM_W + x + 1] - mean);
            if (y + 1 < TM_H)
                s1 += xi * (gen->final_elev[(y + 1) * TM_W + x] - mean);
        }
    }
    st->moran_elev = s0 > 0.0f ? ((float)n / (float)pairs) * (s1 / s0) : 0.0f;

    /* Compression complexity: LZSS bit count over the tile-type stream,
       normalized by the raw size. Low ratio = regular/repetitive map. */
    uint8_t stream[TM_W * TM_H];
    for (int i = 0; i < n; i++)
        stream[i] = gen->tm.tiles[i].type;
    st->compress_ratio = (float)lzss_bits(stream, n) / (8.0f * (float)n);

    /* Connected walkable components (4-connected flood fill) */
    memset(scratch, 0, (size_t)n);
    static const int d4[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    int stack[TM_W * TM_H];
    for (int i = 0; i < n; i++) {
        if (scratch[i] || !tilemap_gen_walkable(rlk_tilemap_get(&gen->tm, i % TM_W, i / TM_W)->type))
            continue;
        int top = 0, size = 0;
        stack[top++] = i;
        scratch[i] = 1;
        while (top > 0) {
            int c = stack[--top];
            size++;
            int cx = c % TM_W, cy = c / TM_W;
            for (int k = 0; k < 4; k++) {
                int nx = cx + d4[k][0], ny = cy + d4[k][1];
                if (nx < 0 || nx >= TM_W || ny < 0 || ny >= TM_H)
                    continue;
                int ni = ny * TM_W + nx;
                if (scratch[ni] || !tilemap_gen_walkable(rlk_tilemap_get(&gen->tm, nx, ny)->type))
                    continue;
                scratch[ni] = 1;
                stack[top++] = ni;
            }
        }
        st->n_components++;
        if (size > st->largest_component)
            st->largest_component = size;
    }

    /* Spatial diversity: number of 4-connected same-type patches. */
    memset(scratch, 0, (size_t)n);
    for (int i = 0; i < n; i++) {
        if (scratch[i])
            continue;
        uint8_t t = rlk_tilemap_get(&gen->tm, i % TM_W, i / TM_W)->type;
        int top = 0;
        stack[top++] = i;
        scratch[i] = 1;
        while (top > 0) {
            int c = stack[--top];
            int cx = c % TM_W, cy = c / TM_W;
            for (int k = 0; k < 4; k++) {
                int nx = cx + d4[k][0], ny = cy + d4[k][1];
                if (nx < 0 || nx >= TM_W || ny < 0 || ny >= TM_H)
                    continue;
                int ni = ny * TM_W + nx;
                if (scratch[ni] || rlk_tilemap_get(&gen->tm, nx, ny)->type != t)
                    continue;
                scratch[ni] = 1;
                stack[top++] = ni;
            }
        }
        st->n_patches++;
    }

    for (int i = 0; i < gen->pois.n; i++) {
        int t = gen->pois.pois[i].type;
        if (t < POI_KIND_COUNT)
            st->n_poi[t]++;
    }
}

void
tilemap_gen_stats_print(const tilemap_gen_t *gen, const tilemap_gen_stats_t *st) {
    static const char *const POI_NAMES[POI_KIND_COUNT] = {
        "cave", "dungeon", "village", "tower", "hamlet", "mine",
        "mill", "port", "lighthouse", "fort"
    };
    printf("=== Map stats (seed %llu, %dx%d) ===\n",
           (unsigned long long)gen->seed, TM_W, TM_H);
    printf("Entropy   : %.3f bits\n", st->entropy);
    printf("Water     : %.1f%%   River: %.1f%%   Walkable land: %.1f%%\n",
           st->water_frac * 100.0f, st->river_frac * 100.0f, st->land_frac * 100.0f);
    printf("Diversity : edge_frac %.3f   pair_entropy %.3f bits   patches %d   moran_elev %+.3f   lzss %.3f\n",
           st->edge_frac, st->pair_entropy, st->n_patches, st->moran_elev,
           st->compress_ratio);
    printf("Components: %d walkable (largest %d tiles = %.1f%% of walkable)\n",
           st->n_components, st->largest_component,
           st->land_frac > 0.0f
               ? 100.0f * (float)st->largest_component / (st->land_frac * (float)(TM_W * TM_H))
               : 0.0f);
    printf("Sinkholes : %d\n", st->n_sinkholes);
    printf("POIs      : ");
    int total = 0;
    for (int i = 0; i < POI_KIND_COUNT; i++) {
        total += st->n_poi[i];
        printf("%s %d%s", POI_NAMES[i], st->n_poi[i], i + 1 < POI_KIND_COUNT ? ", " : "");
    }
    printf("  (total %d)\n", total);
}
