#ifndef RLK_BSP_H
#define RLK_BSP_H

/* layer: 4, domain: worldgen */

/*
 * Binary Space Partitioning for dungeon/room generation.
 *
 * Generates structured dungeons with rectangular rooms connected by
 * L-shaped corridors. Deterministic, zero-allocation (caller provides buffers).
 */

#include <assert.h>
#include <stdint.h>

#include "err.h"
#include "grid.h"
#include "hash.h"
#include "mask.h"
#include "poi.h"
#include "tilemap.h"

typedef struct rlk_bsp_s rlk_bsp_t;

#define RLK_BSP_MAX_NODES 256

/* BSP node: a rectangle in the partition tree.
   split: 0 = leaf, 1 = horizontal (split along Y), 2 = vertical (split along X).
   left/right: child indices, -1 if leaf.
   rx/ry/rw/rh: room rectangle (leaf only, margin applied). */
typedef struct {
    int x, y, w, h;
    int split;
    int split_pos;
    int left, right;
    int rx, ry, rw, rh;
} rlk_bsp_node_t;

/* BSP generator: fill the config fields, call rlk_bsp_init(), then use.
   Zero values are resolved to defaults by rlk_bsp_init().

   Required fields (no default, must be set):
     - write          (non-NULL callback)
     - grid           (non-NULL buffer of w*h bytes, w x h = the fill window)
     - nodes          (non-NULL buffer of RLK_BSP_MAX_NODES nodes)

   Optional fields (0 = use default):
     - min_room_size  -> 5
     - max_depth      -> 7
     - split_bias     -> 0.5 (probability of horizontal split)
     - room_margin    -> 1 (min margin between room and leaf bounds)
     - seed           -> 0 (valid, but use non-zero for variety)
     - write_data     -> NULL (passed through to writer) */
struct rlk_bsp_s {
    /* --- Config (user fills, then call rlk_bsp_init) --- */

    /* Seed for deterministic generation. Same seed + same config = same dungeon.
       Use different seeds for different dungeons. */
    uint64_t seed;

    /* Minimum room dimension (width and height), in cells.
       0 -> 5 (default). Splits stop when a region is smaller than
       2*min_room_size in either dimension. */
    int min_room_size;

    /* Maximum BSP tree depth (number of splits from root to leaf).
       0 -> 7 (default). More = more, smaller rooms. */
    int max_depth;

    /* Probability of a horizontal split (split along Y axis).
       0.0f -> 0.5f (default). 0.0 = always vertical, 1.0 = always horizontal. */
    float split_bias;

    /* Minimum margin between a room and its leaf bounds, in cells.
       0 -> 1 (default). Rooms are inset by a random amount in
       [room_margin, room_margin + 2] from each edge. */
    int room_margin;

    /* Writer callback. Called once per cell with the world position (x, y)
       of the cell's top-left tile. The tile arrives with biome preset to
       the cell type (0 = wall, 1 = floor). Fill type/flags/variant as
       needed; the produced tile is then stamped over the cell's
       cellsize x cellsize tile block (see rlk_bsp_fill).
       Use rlk_bsp_write_wallfloor for simple wall/floor output. */
    void (*write)(const rlk_bsp_t *bsp, const void *data, int x, int y,
                  rlk_tile_t *out);

    /* Opaque user pointer passed as `data` to the writer. May be NULL. */
    const void *write_data;

    /* Caller-provided buffers (must outlive the generator).

       grid: w*h bytes of the fill window. Cell states: 0 = wall, 1 = floor.
       Written by carve_room, carve_corridor. Read by render.

       nodes: RLK_BSP_MAX_NODES nodes. BSP tree, filled by fill. */
    uint8_t *grid;
    rlk_bsp_node_t *nodes;

    /* --- Runtime (filled by rlk_bsp_fill, read-only after) --- */

    /* Window geometry (cells), copied from the fill grid's lattice. */
    int width, height;

    /* Number of BSP nodes created (tree size). */
    int node_count;

    /* Number of rooms (leaf nodes) created. */
    int room_count;
};

/* --- HELPERS ------------------------------------------------------------- */

/* Deterministic per-tile random in [0,1). Salt decorrelates decisions.
   Use inside a custom writer for decoration, treasure placement, etc. */
static inline float
rlk_bsp_rand(const rlk_bsp_t *bsp, int x, int y, uint64_t salt) {
    assert(bsp);
    uint64_t h = rlk_hash_sub(rlk_hash_combine2(bsp->seed, x, y), salt);
    return (float)(h >> 40) * (1.0f / 16777216.0f);
}

/* Ready-made writer data: tile types for wall and floor. */
typedef struct {
    uint8_t wall_type;
    uint8_t floor_type;
} rlk_bsp_wallfloor_t;

/* Ready-made writer: copies wall_type/floor_type from a `rlk_bsp_wallfloor_t`
   given as data. biome is already set by fill (0 = wall, 1 = floor).
   variant is filled with deterministic random. Same role as rlk_ca_write_wallfloor. */
static inline void
rlk_bsp_write_wallfloor(const rlk_bsp_t *bsp, const void *data, int x, int y,
                        rlk_tile_t *out) {
    const rlk_bsp_wallfloor_t *wf = (const rlk_bsp_wallfloor_t *)data;
    out->type = (out->biome == 0) ? wf->wall_type : wf->floor_type;
    out->variant = (uint8_t)(rlk_bsp_rand(bsp, x, y, 0xB5B5) * 255.0f);
}

/* --- PUBLIC API ---------------------------------------------------------- */

/* Resolve zero defaults and validate (safety asserts).
   Call once after filling the config fields. */
void rlk_bsp_init(rlk_bsp_t *bsp);

/* Generate the dungeon (BSP split, rooms, corridors) and render onto tm over
   the window g->lattice: a world rect of w x h cells of `cellsize` tiles.
   g->v is not read (window geometry only). Buffers grid/nodes must match
   the window. RLK_E_INVALID if tm does not fully cover the window.

   Rendering: the writer is called once per cell, then the produced tile is
   stamped over the cell's cellsize x cellsize tile block, except:
     - cells with mask weight 0 are not written (mask NULL = all allowed;
       the mask geometry must equal g->lattice);
     - tiles inside the bbox of a POI of `poi` are not written (reserved
       areas, poi NULL = none).

   spawn (NULL = ignored): on output, the entrance POI — anchor at the
   centre of the largest room, bbox = its world rect, seed =
   rlk_poi_seed(bsp->seed, ...). type/gen/level are left as the caller set
   them. Not written if there is no room.

   Not const: mutates bsp->grid, bsp->nodes and the runtime fields. */
rlk_err_t rlk_bsp_fill(rlk_bsp_t *bsp, const rlk_grid_t *g, rlk_tilemap_t *tm,
                       const rlk_mask_t *mask, const rlk_poi_list_t *poi,
                       rlk_poi_t *spawn);

#endif /* RLK_BSP_H */
