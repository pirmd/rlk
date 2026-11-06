#ifndef GIS_EXPORT_H
#define GIS_EXPORT_H

/*
 * gis_export - raster export helpers for external GIS tools (demo component,
 * reusable by any rlk-based project: only depends on the lib lattices/grids).
 *
 *   gis_export_asc()        ESRI ASCII grid from a float raster, georeferenced
 *                           (xllcorner/yllcorner/cellsize from the lattice,
 *                           NODATA for NaN cells, rows written north-first so a
 *                           parse-back maps file line l to cell row j = h-1-l,
 *                           consistent with rlk_grid_center)
 *   gis_export_pgm()        binary PGM (P5) from a byte raster, top-first
 *   gis_export_grid_pgm()   PGM of a float grid normalized [vmin,vmax] -> [0,255]
 *                           (clamped; vmin == vmax maps to 0), scratch = w*h
 *                           caller-owned bytes
 *
 * Header-only, no allocation, all return 0 on success, -1 on I/O error.
 *
 *   #include "gis_export.h"   (demo built with -Itools)
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "grid.h"

/* ESRI ASCII grid from a float raster; NaN cells are written as nodata. */
static inline int
gis_export_asc(const char *path, const rlk_lattice_t *lat, const float *v,
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
gis_export_pgm(const char *path, int w, int h, const uint8_t *v) {
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
static inline int
gis_export_grid_pgm(const char *path, const rlk_grid_t *g,
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
    return gis_export_pgm(path, g->lattice.w, g->lattice.h, scratch);
}

#endif /* GIS_EXPORT_H */
