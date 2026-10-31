/*
 * demo_dump - generate a map and dump its rasters to .asc/.pgm files for
 * external validation (QGIS/GDAL). Terminal-only.
 *
 *   demo_dump [seed] [outdir]     (defaults: seed 42, outdir ".")
 *
 * Writes <outdir>/<seed>_{elev,filled,acc,lake,slope}.asc (coarse hydrology
 * grid, ESRI ASCII) and <outdir>/<seed>_{strahler,river_mask,river_carve,
 * final_elev}.pgm (binary PGM). rc: 0 ok, 1 I/O error, 2 usage.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rlk.h"
#include "tilemap_gen.h"
#include "demo_dump.h"

#define SEED 42ULL

int
main(int argc, char **argv) {
    if (argc > 3) {
        fprintf(stderr, "usage: %s [seed] [outdir]\n", argv[0]);
        return 2;
    }
    uint64_t seed = (argc > 1) ? strtoull(argv[1], NULL, 0) : SEED;
    const char *outdir = (argc > 2) ? argv[2] : ".";

    static tilemap_gen_t gen;
    static float fbuf[GRID_W * GRID_H];      /* u16/u32 -> float conversions */
    static uint8_t pgm[TM_W * TM_H];         /* PGM scratch (>= GRID_W*GRID_H) */

    tilemap_gen_generate(&gen, seed);

    char path[512];
    int rc = 0;

    /* Coarse hydrology grid (ESRI ASCII, georeferenced via the lattice). */
    const rlk_lattice_t *lat = &gen.elev_grid.lattice;
    const int gn = GRID_W * GRID_H;

    snprintf(path, sizeof path, "%s/%llu_elev.asc", outdir, (unsigned long long)seed);
    rc |= demo_dump_asc(path, lat, gen.elev_grid.values, -9999.0f);

    for (int k = 0; k < gn; k++)
        fbuf[k] = (float)gen.drain.filled[k] / (float)RLK_DRAIN_SCALE;
    snprintf(path, sizeof path, "%s/%llu_filled.asc", outdir, (unsigned long long)seed);
    rc |= demo_dump_asc(path, lat, fbuf, -9999.0f);

    for (int k = 0; k < gn; k++)
        fbuf[k] = (float)gen.drain.acc[k];
    snprintf(path, sizeof path, "%s/%llu_acc.asc", outdir, (unsigned long long)seed);
    rc |= demo_dump_asc(path, lat, fbuf, -9999.0f);

    snprintf(path, sizeof path, "%s/%llu_lake.asc", outdir, (unsigned long long)seed);
    rc |= demo_dump_asc(path, &gen.lake_grid.lattice, gen.lake_grid.values, 0.0f);

    rlk_grid_t slope_grid = { .lattice = gen.drain.lattice, .values = gen.hydro_slope };
    snprintf(path, sizeof path, "%s/%llu_slope.asc", outdir, (unsigned long long)seed);
    rc |= demo_dump_asc(path, &slope_grid.lattice, slope_grid.values, -9999.0f);

    /* Byte rasters (PGM, top-first). */
    snprintf(path, sizeof path, "%s/%llu_strahler.pgm", outdir, (unsigned long long)seed);
    rc |= demo_dump_pgm(path, GRID_W, GRID_H, gen.hydro_strahler);

    /* Per-tile fields, sampled on the tilemap lattice (cellsize 1). */
    rlk_grid_t tile_grid = { .lattice = rlk_tilemap_lattice(&gen.tm), .values = gen.final_elev };
    float lo = 0.0f, hi = 1.0f;
    rlk_grid_minmax(&tile_grid, &lo, &hi);
    snprintf(path, sizeof path, "%s/%llu_final_elev.pgm", outdir, (unsigned long long)seed);
    rc |= demo_dump_grid_pgm(path, &tile_grid, lo, hi, pgm);

    tile_grid.values = gen.river_mask_buf;
    snprintf(path, sizeof path, "%s/%llu_river_mask.pgm", outdir, (unsigned long long)seed);
    rc |= demo_dump_grid_pgm(path, &tile_grid, 0.0f, 1.0f, pgm);

    tile_grid.values = gen.river_carve_buf;
    rlk_grid_minmax(&tile_grid, &lo, &hi);
    snprintf(path, sizeof path, "%s/%llu_river_carve.pgm", outdir, (unsigned long long)seed);
    rc |= demo_dump_grid_pgm(path, &tile_grid, lo, hi, pgm);

    if (rc) {
        fprintf(stderr, "demo_dump: I/O error writing to %s\n", outdir);
        return 1;
    }
    printf("demo_dump: seed %llu -> %s/%llu_{elev,filled,acc,lake,slope}.asc, "
           "%llu_{strahler,final_elev,river_mask,river_carve}.pgm\n",
           (unsigned long long)seed, outdir,
           (unsigned long long)seed, (unsigned long long)seed);
    return 0;
}
