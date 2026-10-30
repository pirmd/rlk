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
static int
stats_walkable(uint8_t type) {
    switch (type) {
    case TILE_GRASS: case TILE_SAND: case TILE_TREE: case TILE_SNOW:
    case TILE_MARSH: case TILE_FLOOR: case TILE_PINE: case TILE_PLAZA:
    case TILE_DOCK: case TILE_SINKHOLE:
        return 1;
    default:
        return 0;
    }
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
        if (stats_walkable(t)) n_land++;
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

    /* Connected walkable components (4-connected flood fill) */
    memset(scratch, 0, (size_t)n);
    static const int d4[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    int stack[TM_W * TM_H];
    for (int i = 0; i < n; i++) {
        if (scratch[i] || !stats_walkable(rlk_tilemap_get(&gen->tm, i % TM_W, i / TM_W)->type))
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
                if (scratch[ni] || !stats_walkable(rlk_tilemap_get(&gen->tm, nx, ny)->type))
                    continue;
                scratch[ni] = 1;
                stack[top++] = ni;
            }
        }
        st->n_components++;
        if (size > st->largest_component)
            st->largest_component = size;
    }

    for (int i = 0; i < gen->pois.n; i++) {
        uint32_t t = gen->pois.v[i].type;
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
