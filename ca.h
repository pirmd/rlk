#ifndef RLK_CA_H
#define RLK_CA_H

/*
 * Cellular Automata for cave/dungeon generation.
 *
 * Generates organic, natural-looking cave structures using Cellular Automata.
 * Pure function, deterministic, zero-allocation (caller provides buffers).
 */

#include <assert.h>
#include <stdint.h>

#include "err.h"
#include "grid.h"
#include "hash.h"
#include "mask.h"
#include "poi.h"
#include "tilemap.h"

typedef struct rlk_ca_s rlk_ca_t;

/* CA generator: fill the config fields, call rlk_ca_init(), then use.
   Zero values are resolved to defaults by rlk_ca_init().

   Required fields (no default, must be set):
     - write           (non-NULL callback)
     - grid, region_ids (non-NULL buffers of w*h elements, where w x h is
                         the fill window, see rlk_ca_fill)

   Optional fields (0 = use default):
     - fill_prob      -> 0.45
     - iterations     -> 5
     - birth_low/high -> 4/5
     - survive_low/high -> 4/8
     - connect        -> 0 (skip; set to 1 to enable)
     - seed           -> 0 (valid, but use non-zero for variety)
     - write_data     -> NULL (passed through to writer) */
struct rlk_ca_s {
    /* --- Config (user fills, then call rlk_ca_init) --- */

    /* Seed for deterministic generation. Same seed + same config = same cave.
       Use different seeds for different caves. */
    uint64_t seed;

    /* Initial floor probability per cell, in [0,1].
       0 -> 0.45 (default). Typical range: 0.40 .. 0.55.
       Higher = more open caves, lower = more walls. */
    float fill_prob;

    /* Birth rule: a wall cell becomes floor if it has birth_low..birth_high
       floor neighbors (8-connected, range 0..8).
       0,0 -> 4,5 (default). Higher = floor expands more aggressively. */
    int birth_low, birth_high;

    /* Survive rule: a floor cell stays floor if it has survive_low..survive_high
       floor neighbors (8-connected, range 0..8).
       0,0 -> 4,8 (default). Lower low / higher high = floor survives more. */
    int survive_low, survive_high;

    /* Number of CA iterations (smoothing passes).
       0 -> 5 (default). More = smoother caves but slower. */
    int iterations;

    /* Connectivity: 0 = skip (regions stay separate);
       > 0 = carve corridors to connect all floor regions into one.
       Not resolved from 0: 0 explicitly means "do not connect".
       Performance: O(N^2 * W^2 * H^2) where N = region count.
       Practical for grids up to ~128x128 with few regions (typically 1-5).
       For larger grids, keep connect = 0. */
    int connect;

    /* Writer callback. Called once per cell with the world position (x, y)
       of the cell's top-left tile. The tile arrives with biome preset to
       the region id (0 = wall, > 0 = floor region). Fill type/flags/variant
       as needed; the produced tile is then stamped over the cell's
       cellsize x cellsize tile block (see rlk_ca_fill).
       Use rlk_ca_write_wallfloor for simple wall/floor output. */
    void (*write)(const rlk_ca_t *ca, const void *data, int x, int y,
                  rlk_tile_t *out);

    /* Opaque user pointer passed as `data` to the writer. May be NULL. */
    const void *write_data;

    /* Caller-provided buffers (must outlive the generator).

       grid: w*h bytes of the fill window. CA cell states: 0 = wall, 1 = floor.
       Written by seed_grid, step, connect_regions. Read by render.

       region_ids: w*h ints of the fill window. Multi-use scratch:
         - During CA iterations: first W*H bytes used as double-buffer.
         - find_regions: union-find parent array, then compact labels.
         - Render: region id per cell (0 = wall, 1..N = floor region). */
    uint8_t *grid;
    int *region_ids;

    /* --- Runtime (filled by rlk_ca_fill, read-only after) --- */

    /* Window geometry (cells), copied from the fill grid's lattice. */
    int width, height;

    /* Number of connected floor regions after fill.
       0 = no floor, 1 = all floor connected (after connect), >1 = disconnected. */
    int region_count;

    /* Number of CA iterations actually executed (= config iterations). */
    int cur_iter;
};

/* --- HELPERS ------------------------------------------------------------- */

/* Deterministic per-tile random in [0,1). Salt decorrelates decisions.
   Use inside a custom writer for moss, cracks, stalagmites, etc. */
static inline float
rlk_ca_rand(const rlk_ca_t *ca, int x, int y, uint64_t salt) {
    assert(ca);
    uint64_t h = rlk_hash_sub(rlk_hash_combine2(ca->seed, x, y), salt);
    return (float)(h >> 40) * (1.0f / 16777216.0f);
}

/* Ready-made writer data: tile types for wall and floor. */
typedef struct {
    uint8_t wall_type;
    uint8_t floor_type;
} rlk_ca_wallfloor_t;

/* Ready-made writer: copies wall_type/floor_type from a `rlk_ca_wallfloor_t`
   given as data. biome is already set by fill (0 = wall, > 0 = floor region).
   variant is filled with deterministic random. Same role as rlk_wg_write_const. */
static inline void
rlk_ca_write_wallfloor(const rlk_ca_t *ca, const void *data, int x, int y,
                       rlk_tile_t *out) {
    const rlk_ca_wallfloor_t *wf = (const rlk_ca_wallfloor_t *)data;
    out->type = (out->biome == 0) ? wf->wall_type : wf->floor_type;
    out->variant = (uint8_t)(rlk_ca_rand(ca, x, y, 0xCAFE) * 255.0f);
}

/* --- PUBLIC API ---------------------------------------------------------- */

/* Resolve zero defaults and validate (safety asserts).
   Call once after filling the config fields. */
void rlk_ca_init(rlk_ca_t *ca);

/* Generate (seed, iterate, connect) and render onto tm over the window
   g->lattice: a world rect of w x h cells of `cellsize` tiles. g->v is not
   read (window geometry only). Buffers grid/region_ids must hold w*h
   elements of that window. RLK_E_INVALID if tm does not fully cover the
   window.

   Rendering: the writer is called once per cell, then the produced tile is
   stamped over the cell's cellsize x cellsize tile block, except:
     - cells with mask weight 0 are not written (mask NULL = all allowed;
       the mask geometry must equal g->lattice);
     - tiles inside the bbox of a POI of `poi` are not written (reserved
       areas, poi NULL = none).

   spawn (NULL = ignored): on output, the entrance POI — anchor on the
   floor cell nearest the centre of the largest floor region (the bbox
   centre itself may be rock), bbox = the region's world rect, seed =
   rlk_poi_seed(ca->seed, ...). type/gen/level are left as the caller set
   them. Not written if there is no floor region.

   Not const: mutates ca->grid, ca->region_ids and the runtime fields. */
rlk_err_t rlk_ca_fill(rlk_ca_t *ca, const rlk_grid_t *g, rlk_tilemap_t *tm,
                      const rlk_mask_t *mask, const rlk_poi_list_t *poi,
                      rlk_poi_t *spawn);

#endif /* RLK_CA_H */
