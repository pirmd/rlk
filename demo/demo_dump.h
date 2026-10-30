#ifndef DEMO_DUMP_H
#define DEMO_DUMP_H

/*
 * demo_dump - export rlk rasters to ESRI ASCII (.asc) and binary PGM (.pgm)
 * for external validation (QGIS/GDAL). Demo-only, header-only, zero
 * allocation (the PGM scratch is caller-provided).
 *
 * .asc georeferencing: xllcorner = origin_x, yllcorner = origin_y,
 * cellsize, NODATA_value in the header; data rows are written north-first
 * (j = h-1 .. 0), so a parse-back maps file line l back to cell row
 * j = h-1-l, consistent with rlk_grid_center (cell (i,j) centre at world
 * (origin_x + (i+0.5)*cellsize, origin_y + (j+0.5)*cellsize), y growing
 * northward in world coordinates).
 *
 * All functions return 0 on success, -1 on I/O error.
 *
 *   demo_dump_asc("elev.asc", &g.lattice, g.v, -9999.0f);
 *   demo_dump_grid_pgm("elev.pgm", &g, 0.0f, 1.0f, scratch);
 *   demo_dump_mask_pgm("mask.pgm", &m);
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "geom.h"
#include "grid.h"
#include "mask.h"

/* ESRI ASCII grid from a float raster; NaN cells are written as nodata. */
static inline int
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
static inline int
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

/* PGM of a mask (values are already bytes). */
static inline int
demo_dump_mask_pgm(const char *path, const rlk_mask_t *m) {
    assert(m);
    return demo_dump_pgm(path, m->lattice.w, m->lattice.h, m->v);
}

/* PGM of a float grid, normalized [vmin,vmax] -> [0,255] (clamped;
   vmin == vmax maps to 0). scratch = w*h bytes, caller-owned. */
static inline int
demo_dump_grid_pgm(const char *path, const rlk_grid_t *g,
                   float vmin, float vmax, uint8_t *scratch) {
    assert(g && scratch && g->lattice.w > 0 && g->lattice.h > 0);
    const int n = g->lattice.w * g->lattice.h;
    const float span = vmax - vmin;
    for (int k = 0; k < n; k++) {
        float t = span != 0.0f ? (g->v[k] - vmin) / span : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        scratch[k] = (uint8_t)(t * 255.0f + 0.5f);
    }
    return demo_dump_pgm(path, g->lattice.w, g->lattice.h, scratch);
}

#endif /* DEMO_DUMP_H */
