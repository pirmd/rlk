/*
 * test_tilemap_gen - non-regression test of the tilemap_gen demo pipeline
 * (lives in demo/ because it links the tilemap_gen objects; run by `make check`).
 *
 * Generates the map on a fixed set of seeds and compares the map probes
 * (tilemap_gen_stats) against golden values captured at a reference commit:
 * any drift in the deterministic per-seed output fails the run.
 * Ref: arxiv 2201.10334 (objective metrics for procgen levels).
 *
 *   test_tilemap_gen           check all seeds against GOLDEN, rc = 1 on drift
 *   test_tilemap_gen record    print the current stats as a GOLDEN table to paste
 *   test_tilemap_gen report [n]  multi-seed metrics table (seeds 1..n, default 25)
 *                              with min/max/mean, cross-seed Hamming diversity and
 *                              a playability proxy (POI pair reachability via
 *                              rlk_path_find); exploratory, not part of the golden check
 *
 * Two complementary checks per seed:
 * - determinism property: the seed is generated twice, tilemap hashes must
 *   match (never needs re-record, catches non-determinism only);
 * - golden snapshot: stats compared against GOLDEN.
 * Golden policy: floats compared with tolerance (entropy 1e-3, fractions
 * 1e-4), integer counts exactly. Re-record only on an intentional change of
 * the generation output.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rlk.h"
#include "tilemap_gen.h"

typedef struct {
    float entropy;       /* Shannon entropy of tile types, bits */
    float water_frac;    /* WATER + LAKE fraction */
    float river_frac;    /* RIVER fraction */
    float land_frac;     /* walkable land fraction */
    float edge_frac;     /* adjacent tile pairs with different types */
    float pair_entropy;  /* Shannon entropy of adjacent tile pairs, bits */
    float moran_elev;    /* Moran's I of the elevation field */
    float compress_ratio; /* LZSS bits / raw bits of the tilemap */
    int n_components;    /* connected walkable components */
    int largest_component;
    int n_patches;       /* connected same-type patches */
    int n_sinkholes;
    int n_poi_total;
} golden_t;

static const uint64_t SEEDS[] = { 42ULL, 7ULL, 1ULL, 1337ULL, 2026ULL };
#define N_SEEDS ((int)(sizeof SEEDS / sizeof SEEDS[0]))

/* Golden values captured 2026-10-30 (refactor/next), re-recorded after the
   specialist-review changes of 2026-10-30 (Hack bank fade via acc_log_gain,
   latitudinal gradient D_TEMP_LAT on the Köppen axis), re-recorded again
   2026-11-05 to add the spatial-diversity probes (edge_frac, n_patches,
   pair_entropy, moran_elev), re-recorded again 2026-11-05 after clamping
   gen_relief_elev_fn to [0, RLK_DRAIN_SCALE) (drain contract; only seeds
   with peak elevation in (1-1/256, 1) drifted), and again 2026-11-05 after the
   hydrology pass (coastal carve taper, pure river mask + slot_lake, channel
   width cap cellsize/2, continuous float-SDF meanders, lake threshold 0.05,
   marsh restricted to the channel vicinity). Re-record with
   `test_tilemap_gen record` only on an intentional change of the
   generation or stats output. */
static const golden_t GOLDEN[N_SEEDS] = {
    /* entropy    water     river    land     edge     pairH    moran    lzss     comps largest patch sink poi */
    { 3.015760f, 0.245768f, 0.037760f, 0.674561f, 0.310241f, 4.484357f, 0.978272f, 0.350576f, 39, 5127, 1407, 50, 22 },  /* seed 42 */
    { 2.519897f, 0.391276f, 0.012939f, 0.558838f, 0.241705f, 3.687113f, 0.948161f, 0.286641f, 52, 6288, 917, 25, 19 },  /* seed 7 */
    { 2.858118f, 0.249105f, 0.013265f, 0.708740f, 0.278704f, 4.168202f, 0.969678f, 0.329590f, 46, 8551, 1160, 41, 27 },  /* seed 1 */
    { 3.141278f, 0.091471f, 0.066081f, 0.686605f, 0.237927f, 4.411116f, 0.986673f, 0.306549f, 48, 4772, 935, 35, 24 },  /* seed 1337 */
    { 3.065853f, 0.130452f, 0.049398f, 0.684326f, 0.299893f, 4.511510f, 0.976908f, 0.338806f, 44, 6714, 1261, 54, 30 },  /* seed 2026 */
};

#define ENT_TOL   1e-3f
#define FRAC_TOL  1e-4f

/* Determinism property: hash of the whole tilemap for a given seed. */
static uint64_t
tilemap_hash(const tilemap_gen_t *gen) {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < TM_W * TM_H; i++) {
        const rlk_tile_t *t = &gen->tm.tiles[i];
        h = rlk_hash_combine2(h, t->type, t->flags);
        h = rlk_hash_combine2(h, t->biome, t->variant);
        h = rlk_hash_combine2(h, t->depth, i);
    }
    return h;
}

static int
check_one(uint64_t seed, const golden_t *g, const tilemap_gen_stats_t *st) {
    int bad = 0;
    int poi_total = 0;
    for (int i = 0; i < POI_KIND_COUNT; i++)
        poi_total += st->n_poi[i];
    if (fabsf(st->entropy - g->entropy) > ENT_TOL) {
        printf("  seed %llu FAIL entropy      %.4f != %.4f\n",
               (unsigned long long)seed, st->entropy, g->entropy);
        bad = 1;
    }
    if (fabsf(st->water_frac - g->water_frac) > FRAC_TOL) {
        printf("  seed %llu FAIL water_frac   %.5f != %.5f\n",
               (unsigned long long)seed, st->water_frac, g->water_frac);
        bad = 1;
    }
    if (fabsf(st->river_frac - g->river_frac) > FRAC_TOL) {
        printf("  seed %llu FAIL river_frac   %.5f != %.5f\n",
               (unsigned long long)seed, st->river_frac, g->river_frac);
        bad = 1;
    }
    if (fabsf(st->land_frac - g->land_frac) > FRAC_TOL) {
        printf("  seed %llu FAIL land_frac    %.5f != %.5f\n",
               (unsigned long long)seed, st->land_frac, g->land_frac);
        bad = 1;
    }
    if (fabsf(st->edge_frac - g->edge_frac) > FRAC_TOL) {
        printf("  seed %llu FAIL edge_frac    %.5f != %.5f\n",
               (unsigned long long)seed, st->edge_frac, g->edge_frac);
        bad = 1;
    }
    if (fabsf(st->pair_entropy - g->pair_entropy) > ENT_TOL) {
        printf("  seed %llu FAIL pair_entropy %.5f != %.5f\n",
               (unsigned long long)seed, st->pair_entropy, g->pair_entropy);
        bad = 1;
    }
    if (fabsf(st->moran_elev - g->moran_elev) > ENT_TOL) {
        printf("  seed %llu FAIL moran_elev   %.5f != %.5f\n",
               (unsigned long long)seed, st->moran_elev, g->moran_elev);
        bad = 1;
    }
    if (fabsf(st->compress_ratio - g->compress_ratio) > FRAC_TOL) {
        printf("  seed %llu FAIL lzss_ratio   %.5f != %.5f\n",
               (unsigned long long)seed, st->compress_ratio, g->compress_ratio);
        bad = 1;
    }
    if (st->n_components != g->n_components) {
        printf("  seed %llu FAIL n_components %d != %d\n",
               (unsigned long long)seed, st->n_components, g->n_components);
        bad = 1;
    }
    if (st->largest_component != g->largest_component) {
        printf("  seed %llu FAIL largest_comp %d != %d\n",
               (unsigned long long)seed, st->largest_component, g->largest_component);
        bad = 1;
    }
    if (st->n_patches != g->n_patches) {
        printf("  seed %llu FAIL n_patches    %d != %d\n",
               (unsigned long long)seed, st->n_patches, g->n_patches);
        bad = 1;
    }
    if (st->n_sinkholes != g->n_sinkholes) {
        printf("  seed %llu FAIL n_sinkholes  %d != %d\n",
               (unsigned long long)seed, st->n_sinkholes, g->n_sinkholes);
        bad = 1;
    }
    if (poi_total != g->n_poi_total) {
        printf("  seed %llu FAIL n_poi_total  %d != %d\n",
               (unsigned long long)seed, poi_total, g->n_poi_total);
        bad = 1;
    }
    return bad;
}

static void
record_one(uint64_t seed, const tilemap_gen_stats_t *st) {
    int poi_total = 0;
    for (int i = 0; i < POI_KIND_COUNT; i++)
        poi_total += st->n_poi[i];
    printf("    { %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %d, %d, %d, %d, %d },  /* seed %llu */\n",
           st->entropy, st->water_frac, st->river_frac, st->land_frac, st->edge_frac,
           st->pair_entropy, st->moran_elev, st->compress_ratio,
           st->n_components, st->largest_component, st->n_patches, st->n_sinkholes,
           poi_total, (unsigned long long)seed);
}

/* Report mode: run seeds 1..n (default 25) and print one row of metrics per
   seed plus min/max/mean, the mean pairwise Hamming distance between the
   generated maps (cross-seed diversity) and a playability proxy (fraction of
   surface-POI pairs connected by a walkable path, mean path length).
   Not part of the golden check. */
#define REPORT_MAX_SEEDS 64

/* Nearest walkable tile to (x, y), Chebyshev rings, fixed scan order. */
static int
snap_walkable(const uint8_t *cost, int x, int y, int *ox, int *oy) {
    for (int r = 0; r < 8; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx != -r && dx != r && dy != -r && dy != r)
                    continue;  /* ring only */
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= TM_W || ny >= TM_H)
                    continue;
                if (cost[ny * TM_W + nx] != RLK_PATH_BLOCKED) {
                    *ox = nx;
                    *oy = ny;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Playability proxy: over all pairs of surface settlement POIs (village,
   tower, hamlet, fort), snap anchors to the nearest walkable tile and try
   rlk_path_find. Returns reach fraction in *reach, mean path length (tiles)
   over successful pairs in *mean_len. */
static void
playability(const tilemap_gen_t *gen, float *reach, float *mean_len) {
    static uint8_t cost[TM_W * TM_H];
    static int p_g[TM_W * TM_H], p_parent[TM_W * TM_H], p_heap[TM_W * TM_H];
    static int p_heap_pos[TM_W * TM_H], p_f[TM_W * TM_H];
    static uint8_t p_closed[TM_W * TM_H];
    static int path[2 * TM_W * TM_H];

    for (int i = 0; i < TM_W * TM_H; i++)
        cost[i] = tilemap_gen_walkable(gen->tm.tiles[i].type)
                      ? 1 : RLK_PATH_BLOCKED;
    rlk_path_cost_t pcost = { .lattice = rlk_tilemap_lattice(&gen->tm),
                              .table = cost };
    rlk_path_scratch_t scr = { p_g, p_parent, p_heap, p_heap_pos, p_f, p_closed };

    int sites[64], nsites = 0;
    for (int i = 0; i < gen->pois.n && nsites < 64; i++) {
        const rlk_poi_t *p = &gen->pois.pois[i];
        if (p->gen != 0)
            continue;
        if (p->type == POI_VILLAGE || p->type == POI_TOWER
            || p->type == POI_HAMLET || p->type == POI_FORT)
            sites[nsites++] = i;
    }

    int pairs = 0, ok = 0, len_sum = 0;
    for (int a = 0; a < nsites; a++) {
        for (int b = a + 1; b < nsites; b++) {
            int ax, ay, bx, by;
            if (!snap_walkable(cost, gen->pois.pois[sites[a]].x,
                               gen->pois.pois[sites[a]].y, &ax, &ay))
                continue;
            if (!snap_walkable(cost, gen->pois.pois[sites[b]].x,
                               gen->pois.pois[sites[b]].y, &bx, &by))
                continue;
            int n = 0;
            pairs++;
            if (rlk_path_find(&pcost, &scr, ax, ay, bx, by,
                              path, TM_W * TM_H, &n) == RLK_OK) {
                ok++;
                len_sum += n - 1;
            }
        }
    }
    *reach = pairs > 0 ? (float)ok / (float)pairs : 1.0f;
    *mean_len = ok > 0 ? (float)len_sum / (float)ok : 0.0f;
}

static void
report(int nseeds) {
    static tilemap_gen_t gen;
    static uint8_t scratch[TM_W * TM_H];
    static uint8_t maps[REPORT_MAX_SEEDS][TM_W * TM_H];
    tilemap_gen_stats_t st[REPORT_MAX_SEEDS];
    float reach[REPORT_MAX_SEEDS], rlen[REPORT_MAX_SEEDS];

    printf(" seed   entr  pairH  edge  moran   lzss  water%% river%% land%%  comps largest%% patch  sink   poi  poiR%%  road\n");
    for (int s = 0; s < nseeds; s++) {
        tilemap_gen_generate(&gen, (uint64_t)(s + 1));
        tilemap_gen_stats(&gen, &st[s], scratch);
        playability(&gen, &reach[s], &rlen[s]);
        int poi_total = 0;
        for (int i = 0; i < POI_KIND_COUNT; i++)
            poi_total += st[s].n_poi[i];
        for (int i = 0; i < TM_W * TM_H; i++)
            maps[s][i] = gen.tm.tiles[i].type;
        printf("%5d %5.2f %6.2f %5.3f %+5.2f %6.3f %6.1f %6.1f %6.1f %6d %8.1f %5d %5d %4d %6.1f %5.0f\n",
               s + 1, st[s].entropy, st[s].pair_entropy, st[s].edge_frac,
               st[s].moran_elev, st[s].compress_ratio,
               st[s].water_frac * 100.0f,
               st[s].river_frac * 100.0f, st[s].land_frac * 100.0f,
               st[s].n_components,
               100.0f * (float)st[s].largest_component / (float)(TM_W * TM_H),
               st[s].n_patches, st[s].n_sinkholes, poi_total,
               reach[s] * 100.0f, rlen[s]);
    }

    /* Aggregates over the seeds. */
    printf(" min/max/mean over %d seeds:\n", nseeds);
#define REPORT_NCOLS 15
    float col[REPORT_MAX_SEEDS];
    const char *names[REPORT_NCOLS] = { "entropy", "pairH", "edge", "moran", "lzss",
                                        "water%", "river%", "land%", "comps", "largest%",
                                        "patch", "sink", "poi", "poiR%", "road" };
    for (int c = 0; c < REPORT_NCOLS; c++) {
        float mn = 1e30f, mx = -1e30f, mean = 0.0f;
        for (int s = 0; s < nseeds; s++) {
            int poi_total = 0;
            for (int i = 0; i < POI_KIND_COUNT; i++)
                poi_total += st[s].n_poi[i];
            switch (c) {
            case 0: col[s] = st[s].entropy; break;
            case 1: col[s] = st[s].pair_entropy; break;
            case 2: col[s] = st[s].edge_frac; break;
            case 3: col[s] = st[s].moran_elev; break;
            case 4: col[s] = st[s].compress_ratio; break;
            case 5: col[s] = st[s].water_frac * 100.0f; break;
            case 6: col[s] = st[s].river_frac * 100.0f; break;
            case 7: col[s] = st[s].land_frac * 100.0f; break;
            case 8: col[s] = (float)st[s].n_components; break;
            case 9: col[s] = 100.0f * (float)st[s].largest_component / (float)(TM_W * TM_H); break;
            case 10: col[s] = (float)st[s].n_patches; break;
            case 11: col[s] = (float)st[s].n_sinkholes; break;
            case 12: col[s] = (float)poi_total; break;
            case 13: col[s] = reach[s] * 100.0f; break;
            default: col[s] = rlen[s]; break;
            }
            if (col[s] < mn) mn = col[s];
            if (col[s] > mx) mx = col[s];
            mean += col[s];
        }
        printf("  %-8s min %7.3f  max %7.3f  mean %7.3f\n",
               names[c], mn, mx, mean / (float)nseeds);
    }

    /* Cross-seed diversity: mean pairwise Hamming distance between maps. */
    double acc = 0.0;
    int npairs = 0;
    for (int a = 0; a < nseeds; a++) {
        for (int b = a + 1; b < nseeds; b++) {
            int diff = 0;
            for (int i = 0; i < TM_W * TM_H; i++)
                if (maps[a][i] != maps[b][i]) diff++;
            acc += (double)diff / (double)(TM_W * TM_H);
            npairs++;
        }
    }
    printf(" cross-seed mean pairwise Hamming distance: %.4f (%d pairs)\n",
           acc / (double)npairs, npairs);
}

int
main(int argc, char **argv) {
    int record = argc > 1 && !strcmp(argv[1], "record");
    int report_mode = argc > 1 && !strcmp(argv[1], "report");
    if (argc > 1 && !record && !report_mode) {
        fprintf(stderr, "usage: %s [record|report [nseeds]]\n", argv[0]);
        return 2;
    }

    static tilemap_gen_t gen;
    static uint8_t scratch[TM_W * TM_H];

    if (report_mode) {
        int nseeds = argc > 2 ? atoi(argv[2]) : 25;
        if (nseeds < 2 || nseeds > REPORT_MAX_SEEDS) {
            fprintf(stderr, "nseeds must be in [2,%d]\n", REPORT_MAX_SEEDS);
            return 2;
        }
        report(nseeds);
        return 0;
    }

    if (record) {
        printf("static const golden_t GOLDEN[N_SEEDS] = {\n");
        for (int s = 0; s < N_SEEDS; s++) {
            tilemap_gen_generate(&gen, SEEDS[s]);
            tilemap_gen_stats_t st;
            tilemap_gen_stats(&gen, &st, scratch);
            record_one(SEEDS[s], &st);
        }
        printf("};\n");
        return 0;
    }

    int n_bad = 0;
    for (int s = 0; s < N_SEEDS; s++) {
        tilemap_gen_generate(&gen, SEEDS[s]);
        uint64_t h1 = tilemap_hash(&gen);
        tilemap_gen_generate(&gen, SEEDS[s]);
        uint64_t h2 = tilemap_hash(&gen);
        if (h1 != h2) {
            printf("seed %-5llu FAIL determinism (hash %016llx != %016llx)\n",
                   (unsigned long long)SEEDS[s],
                   (unsigned long long)h1, (unsigned long long)h2);
            n_bad++;
        }
        tilemap_gen_stats_t st;
        tilemap_gen_stats(&gen, &st, scratch);
        int bad = check_one(SEEDS[s], &GOLDEN[s], &st);
        printf("seed %-5llu %s (entropy %.3f, water %.1f%%, components %d, POIs %d)\n",
               (unsigned long long)SEEDS[s], bad ? "FAIL" : "ok  ",
               st.entropy, st.water_frac * 100.0f, st.n_components,
               st.n_poi[0] + st.n_poi[1] + st.n_poi[2] + st.n_poi[3] + st.n_poi[4]
               + st.n_poi[5] + st.n_poi[6] + st.n_poi[7] + st.n_poi[8] + st.n_poi[9]);
        n_bad += bad;
    }
    if (n_bad) {
        printf("test_tilemap_gen: %d/%d seeds failed (golden drift or non-determinism)\n",
               n_bad, N_SEEDS);
        return 1;
    }
    printf("test_tilemap_gen: all %d seeds match golden\n", N_SEEDS);
    return 0;
}
