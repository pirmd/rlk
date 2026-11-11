#ifndef RLK_WORLD_H
#define RLK_WORLD_H

/*
 * Chunked, persistent world on top of a wg pipeline.
 *
 * A world is a wg config (fill w->wg, then rlk_world_init) plus a diff
 * list. Three layers, all pure functions of (seed, wx, wy) plus the diffs:
 *
 * - macro: rlk_world_field samples any wg field at a tile centre, at any
 *   scale, without generating fine tiles -- the macro layer is consultable
 *   without generating the fine chunks. Macro products over a window
 *   (drain/hydro) are composed with rlk_wg_fill_grid + rlk_drain_build.
 * - fine: rlk_world_fill generates one chunk (chunk x chunk tiles) into a
 *   caller tilemap, rlk_world_tile evaluates a single tile. Seams are
 *   coherent by construction: a tile depends only on (seed, wx, wy),
 *   whatever chunk contains it.
 * - diffs: rlk_world_set records a tile modification, applied on every
 *   (re)generation of the tile or its chunk, so a regenerated chunk keeps
 *   the modifications. rlk_world_save / rlk_world_load serialize
 *   (seed + chunk + diffs); the wg config itself is code, not data.
 *
 * Spawn-style queries: rlk_world_find scans a rect with a caller predicate
 * (e.g. "flat and near water", reading rlk_world_field and neighbouring
 * rlk_world_tile), first match in row-major order.
 *
 *   rlk_world_t w = {0};
 *   w.wg.seed = 42;   // ... fields / axes / zones ...
 *   rlk_world_init(&w, 32, diff_buf, 256);
 *   rlk_tilemap_t tm;
 *   rlk_tilemap_init(&tm, 32, 32, tiles, sizeof(tiles));
 *   rlk_world_fill(&w, 3, -1, &tm);   // chunk (3,-1), origin (96,-32)
 */

#include <stdio.h>

#include "err.h"
#include "geom.h"
#include "tilemap.h"
#include "wg.h"

/* A tile modification keyed by world tile; applied when the tile (or its
   chunk) is (re)generated. The payload is the game's rlk_tile_t. */
typedef struct {
    int wx, wy;
    rlk_tile_t tile;
} rlk_world_diff_t;

typedef struct {
    rlk_wg_t wg;              /* pipeline config; fill it, then rlk_world_init */
    int chunk;                /* chunk edge in tiles, > 0 (e.g. 32) */
    rlk_world_diff_t *diffs;  /* caller-provided, diff_cap entries */
    int ndiffs;
    int diff_cap;
} rlk_world_t;

/* Init: asserts chunk > 0 and the diff buffer, resets the diff list, and
   calls rlk_wg_init(&w->wg) (the wg config must be filled beforehand). */
void rlk_world_init(rlk_world_t *w, int chunk, rlk_world_diff_t *diffs, int diff_cap);

/* Macro layer: wg field idx at tile (wx, wy), sampled at the tile centre
   (same convention as the per-tile field pass). Pure function of
   (seed, wx, wy): consultable at any scale without generating fine tiles. */
float rlk_world_field(const rlk_world_t *w, int idx, int wx, int wy);

/* Generate chunk (cx, cy) into tm: tm must be chunk x chunk tiles; its
   origin is set to (cx*chunk, cy*chunk), the pipeline runs over it, then
   the chunk's diffs are applied. */
void rlk_world_fill(const rlk_world_t *w, int cx, int cy, rlk_tilemap_t *tm);

/* Single fine tile at (wx, wy), diff applied. The per-tile primitive. */
void rlk_world_tile(const rlk_world_t *w, int wx, int wy, rlk_tile_t *out);

/* Record a tile modification (replaces an existing diff at the same
   tile). Returns RLK_E_INVALID if the diff list is full. */
rlk_err_t rlk_world_set(rlk_world_t *w, int wx, int wy, rlk_tile_t tile);

/* Predicate over a fine tile, e.g. "flat + near water" via rlk_world_field
   and neighbouring rlk_world_tile. Returns 1 to accept the tile. */
typedef int (*rlk_world_pred_fn)(const rlk_world_t *w, int wx, int wy,
                                 const rlk_tile_t *tile, void *data);

/* First tile of area (world rect, row-major order) accepted by pred.
   RLK_OK with out_wx/out_wy set on match, RLK_E_NOTFOUND if none. */
rlk_err_t rlk_world_find(const rlk_world_t *w, rlk_rect_t area,
                         rlk_world_pred_fn pred, void *data,
                         int *out_wx, int *out_wy);

/* Serialize (seed + chunk + diffs) to f. Native-endian, same-machine
   format. RLK_E_INVALID on I/O failure. */
rlk_err_t rlk_world_save(const rlk_world_t *w, FILE *f);

/* Load a file written by rlk_world_save into w (after rlk_world_init with
   the same wg config): checks the stored seed and chunk against w, fills
   w->diffs, sets w->ndiffs. RLK_E_INVALID on mismatch, truncation or a
   full diff list; on failure the diff list is left empty. */
rlk_err_t rlk_world_load(rlk_world_t *w, FILE *f);

#endif /* RLK_WORLD_H */
