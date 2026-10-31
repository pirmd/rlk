/*
 * demo_metrics - non-regression harness for the tilemap_gen demo.
 *
 * Generates the map on a fixed set of seeds and compares the map probes
 * (tilemap_gen_stats) against golden values captured at a reference commit:
 * any drift in the deterministic per-seed output fails the run.
 * Ref: arxiv 2201.10334 (objective metrics for procgen levels).
 *
 *   demo_metrics           check all seeds against GOLDEN, rc = 1 on drift
 *   demo_metrics record    print the current stats as a GOLDEN table to paste
 *
 * Golden policy: floats compared with tolerance (entropy 1e-3, fractions
 * 1e-4), integer counts exactly. Re-record only on an intentional change of
 * the generation output.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rlk.h"
#include "tilemap_gen.h"

typedef struct {
    float entropy;       /* Shannon entropy of tile types, bits */
    float water_frac;    /* WATER + LAKE fraction */
    float river_frac;    /* RIVER fraction */
    float land_frac;     /* walkable land fraction */
    int n_components;    /* connected walkable components */
    int largest_component;
    int n_sinkholes;
    int n_poi_total;
} golden_t;

static const uint64_t SEEDS[] = { 42ULL, 7ULL, 1ULL, 1337ULL, 2026ULL };
#define N_SEEDS ((int)(sizeof SEEDS / sizeof SEEDS[0]))

/* Golden values captured 2026-10-30 (refactor/next), re-recorded after the
   specialist-review changes of 2026-10-30 (Hack bank fade via D_ACC_LOG,
   latitudinal gradient D_TEMP_LAT on the Köppen axis). Re-record with
   `demo_metrics record` only on an intentional generation change. */
static const golden_t GOLDEN[N_SEEDS] = {
    /* entropy    water     river    land     comps largest sink poi */
    { 3.122547f, 0.259521f, 0.045085f, 0.654541f, 40, 4978, 50, 22 },  /* seed 42 */
    { 2.596394f, 0.398438f, 0.015706f, 0.549967f, 54, 6198, 25, 19 },  /* seed 7 */
    { 3.044841f, 0.271484f, 0.018880f, 0.681315f, 46, 8194, 41, 27 },  /* seed 1 */
    { 3.292967f, 0.113851f, 0.087972f, 0.644613f, 65, 4399, 32, 24 },  /* seed 1337 */
    { 3.193472f, 0.158691f, 0.069417f, 0.636393f, 55, 6194, 52, 30 },  /* seed 2026 */
};

#define ENT_TOL   1e-3f
#define FRAC_TOL  1e-4f

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
    printf("    { %.6ff, %.6ff, %.6ff, %.6ff, %d, %d, %d, %d },  /* seed %llu */\n",
           st->entropy, st->water_frac, st->river_frac, st->land_frac,
           st->n_components, st->largest_component, st->n_sinkholes, poi_total,
           (unsigned long long)seed);
}

int
main(int argc, char **argv) {
    int record = argc > 1 && !strcmp(argv[1], "record");
    if (argc > 1 && !record) {
        fprintf(stderr, "usage: %s [record]\n", argv[0]);
        return 2;
    }

    static tilemap_gen_t gen;
    static uint8_t scratch[TM_W * TM_H];

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
        printf("demo_metrics: %d/%d seeds drifted from golden\n", n_bad, N_SEEDS);
        return 1;
    }
    printf("demo_metrics: all %d seeds match golden\n", N_SEEDS);
    return 0;
}
