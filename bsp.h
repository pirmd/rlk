#ifndef RLK_BSP_H
#define RLK_BSP_H

/*
 * Binary Space Partitioning for dungeon/room generation.
 *
 * Generates structured dungeons with rectangular rooms connected by
 * L-shaped corridors. Deterministic, zero-allocation (caller provides buffers).
 */

#include <stdint.h>

#include "mask.h"
#include "poi.h"
#include "tilemap.h"

/* Max nodes of the BSP tree; size the `nodes` buffer with it. */
#define RLK_BSP_MAX_NODES 256

/* Split kinds (rlk_bsp_node_t.split). */
enum {
    RLK_BSP_LEAF    = 0,  /* leaf node: holds a room */
    RLK_BSP_SPLIT_H = 1,  /* horizontal split (along Y) */
    RLK_BSP_SPLIT_V = 2   /* vertical split (along X) */
};

/* BSP node: a partition rectangle, in window cell coordinates (origin =
   the window's top-left cell). Internal nodes split; leaves hold a room. */
typedef struct {
    int x, y, w, h;      /* node rectangle (cell coords) */
    int split;           /* RLK_BSP_LEAF / RLK_BSP_SPLIT_H / RLK_BSP_SPLIT_V */
    int split_pos;       /* split offset from the node origin (internal nodes) */
    int left, right;     /* child indices, -1 if leaf */
    int rx, ry, rw, rh;  /* room rectangle (leaf only, margin applied) */
} rlk_bsp_node_t;

/* BSP generator. Fill the config fields, call rlk_bsp_init(), then
   rlk_bsp_fill(). Required fields (asserted by rlk_bsp_init): write, grid,
   nodes. Zero config values resolve to the defaults shown, in place. */
typedef struct {
    /* --- Config --- */

    uint64_t seed;       /* random root; same seed + config = same dungeon */
    int min_room_size;   /* min room side, cells; splits stop below 2*this; 0 -> 5 */
    int max_depth;       /* max tree depth; 0 -> 7; more = more, smaller rooms */
    float split_bias;    /* P(horizontal split); 0.0f -> 0.5f; 1.0 = always horizontal */
    int room_margin;     /* min margin room/leaf bounds, cells; 0 -> 1; rooms are
                            inset by a random amount in [margin, margin + 2] */
    void (*write)(uint64_t seed, const void *data, int x, int y,
                  rlk_tile_t *out);  /* per-cell writer (required), see rlk_bsp_fill */
    const void *write_data; /* opaque user pointer passed to write; NULL ok */

    /* --- Buffers (caller-owned, must outlive the generator; w*h of the
           fill window) --- */

    uint8_t *grid;           /* cell states: 0 = wall, 1 = floor */
    rlk_bsp_node_t *nodes;   /* RLK_BSP_MAX_NODES nodes: the BSP tree */

    /* --- Runtime (filled by rlk_bsp_fill) --- */

    int win_w, win_h;    /* window geometry, cells */
    int node_count;      /* nodes created (tree size) */
    int room_count;      /* rooms created (leaf count) */
} rlk_bsp_t;

/* Resolve zero defaults and validate (safety asserts).
   Call once after filling the config fields. */
void rlk_bsp_init(rlk_bsp_t *bsp);

/* Generate the dungeon (BSP split, rooms, corridors) and render onto tm over
   the window `win`: a world rect of w x h cells of `cellsize` tiles.
   Buffers grid/nodes must match the window.
   The tilemap must fully cover the window (asserted).

   Rendering: the writer is called once per cell, then the produced tile is
   stamped over the cell's cellsize x cellsize tile block, except:
     - cells with mask weight 0 are not written (mask NULL = all allowed;
       the mask geometry must equal `win`);
     - tiles inside the bbox of a POI of `poi` are not written (reserved
       areas, poi NULL = none).

   spawn (NULL = ignored): on output, the entrance POI — anchor at the
   centre of the central cell of the largest room, bbox = the room's world
   rect, seed = rlk_poi_seed(bsp->seed, ...). type/gen/level are left as the
   caller set them. Not written if there is no room.

   Not const: mutates bsp->grid, bsp->nodes and the runtime fields. */
void rlk_bsp_fill(rlk_bsp_t *bsp, const rlk_lattice_t *win,
                  rlk_tilemap_t *tm, const rlk_mask_t *mask,
                  const rlk_poi_list_t *poi, rlk_poi_t *spawn);

#endif /* RLK_BSP_H */
