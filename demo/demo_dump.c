/*
 * demo_dump - generate a map and dump its rasters to .asc/.pgm files for
 * external validation (QGIS/GDAL). Terminal-only.
 *
 *   demo_dump [seed] [outdir]     (defaults: seed 42, outdir ".")
 *
 * Writes <outdir>/<seed>_{elev,filled,acc,lake,slope}.asc (coarse hydrology
 * grid, ESRI ASCII) and <outdir>/<seed>_{strahler,river_mask,river_carve,
 * final_elev}.pgm (binary PGM). rc: 0 ok, 1 I/O error, 2 usage.
 *
 * .asc georeferencing: xllcorner = origin_x, yllcorner = origin_y,
 * cellsize, NODATA_value in the header; data rows are written north-first
 * (j = h-1 .. 0), so a parse-back maps file line l back to cell row
 * j = h-1-l, consistent with rlk_grid_center (cell (i,j) centre at world
 * (origin_x + (i+0.5)*cellsize, origin_y + (j+0.5)*cellsize), y growing
 * northward in world coordinates).
 *
 * All export helpers return 0 on success, -1 on I/O error.
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "rlk.h"
#include "tilemap_gen.h"

#define SEED 42ULL

/* ESRI ASCII grid from a float raster; NaN cells are written as nodata. */
static int
demo_dump_asc(const char *path, const rlk_lattice_t *lat, const float *v,
              float nodata) {
    assert(path && lat && v && lat->w > 0 && lat->h > 0);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "ncols %d\nnrows %d\nxllcorner %d\nyllcorner %d\ncellsize %d\nNODATA_value %g\n",
            lat->w, lat->h, lat->origin_x, lat->origin_y, lat->cellsize, (double)nodata);
    for (int j = lat->h - 1; j >= 0; j--) {
        for (int i = 0; i < lat->w; i++) {
            float x = v[(size_t)j * (size_t)lat->w + (size_t)i];
            if (isnan(x)) x = nodata;
            fprintf(f, "%g%c", (double)x, i + 1 < lat->w ? ' ' : '\n');
        }
    }
    return fclose(f) == 0 ? 0 : -1;
}

/* Binary PGM (P5, maxval 255) from a byte raster, row-major, j = 0 (top)
   first, matching the lattice top-left origin. */
static int
demo_dump_pgm(const char *path, int w, int h, const uint8_t *v) {
    assert(path && v && w > 0 && h > 0);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    for (int j = 0; j < h; j++)
        if (fwrite(&v[(size_t)j * (size_t)w], 1, (size_t)w, f) != (size_t)w) {
            fclose(f);
            return -1;
        }
    return fclose(f) == 0 ? 0 : -1;
}

/* PGM of a float grid, normalized [vmin,vmax] -> [0,255] (clamped;
   vmin == vmax maps to 0). scratch = w*h bytes, caller-owned. */
static int
demo_dump_grid_pgm(const char *path, const rlk_grid_t *g,
                   float vmin, float vmax, uint8_t *scratch) {
    assert(g && scratch && g->lattice.w > 0 && g->lattice.h > 0);
    const int n = g->lattice.w * g->lattice.h;
    const float span = vmax - vmin;
    for (int k = 0; k < n; k++) {
        float t = span != 0.0f ? (g->values[k] - vmin) / span : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        scratch[k] = (uint8_t)(t * 255.0f + 0.5f);
    }
    return demo_dump_pgm(path, g->lattice.w, g->lattice.h, scratch);
}

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
