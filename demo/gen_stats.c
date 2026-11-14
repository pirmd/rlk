/*
 * gen_stats - post-generation map probes for the tilemap_gen demo
 * (see tilemap_gen.h): tile-type histogram + water/river/land fractions,
 * then the generic rlk_stats_* kernels over the tile-type stream and the
 * elevation field, connected walkable components (rlk_mask_components),
 * POI counts. Deterministic per seed: usable as a non-regression harness.
 */

#include <stdio.h>
#include <string.h>

#include "gen_internal.h"
#include "mask.h"
#include "stats.h"

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

void
tilemap_gen_stats(const tilemap_gen_t *gen, tilemap_gen_stats_t *st) {
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

    /* Tile-type stream, row-major, then the generic stream probes in one
       rlk_stats_raster call over a single workspace. */
    uint8_t stream[TM_W * TM_H];
    for (int i = 0; i < n; i++)
        stream[i] = gen->tm.tiles[i].type;

    static rlk_stats_ws_unit_t ws[RLK_STATS_WS_UNITS(TM_W, TM_H, TILE_COUNT)];
    rlk_stats_raster_t r;
    rlk_stats_raster(stream, TM_W, TM_H, TILE_COUNT, &r, ws);
    st->entropy = r.entropy;
    st->edge_frac = r.edge_frac;
    st->pair_entropy = r.pair_entropy;
    st->n_patches = r.n_patches;
    st->compress_ratio = r.compress_ratio;

    st->moran_elev = rlk_stats_moran_i(gen->final_elev, TM_W, TM_H);

    /* Connected walkable components (4-connected, via rlk_mask_components) */
    {
        static uint8_t wbuf[TM_W * TM_H];
        static int labels[TM_W * TM_H];
        static int fstack[TM_W * TM_H];
        uint8_t wflags[TILE_COUNT];
        for (int t = 0; t < TILE_COUNT; t++)
            wflags[t] = (uint8_t)(tilemap_gen_walkable((uint8_t)t) ? 1 : 0);
        rlk_tile_props_t wprops = { .flags = wflags, .ntypes = TILE_COUNT };
        rlk_mask_t wm = { .lattice = rlk_tilemap_lattice(&gen->tm), .weights = wbuf };
        assert(rlk_mask_from_tilemap(&wm, &gen->tm, &wprops, 1, NULL) == RLK_OK);
        st->n_components = rlk_mask_components(&wm, labels, fstack);
        static int hist[TM_W * TM_H + 1];
        memset(hist, 0, (size_t)(st->n_components + 1) * sizeof(int));
        for (int i = 0; i < n; i++)
            if (labels[i] > 0)
                hist[labels[i]]++;
        st->largest_component = 0;
        for (int c = 1; c <= st->n_components; c++)
            if (hist[c] > st->largest_component)
                st->largest_component = hist[c];
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
