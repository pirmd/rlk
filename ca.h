#ifndef RLK_CA_H
#define RLK_CA_H

/*
 * Cellular Automata for cave/dungeon generation.
 *
 * Generates organic, natural-looking cave structures using Cellular Automata.
 * Deterministic, zero-allocation (caller provides buffers).
 */

#include <stdint.h>

#include "mask.h"
#include "poi.h"
#include "tilemap.h"

/* CA generator. Fill the config fields, call rlk_ca_init(), then
   rlk_ca_fill(). Required fields (asserted by rlk_ca_init): write, grid,
   region_ids. Zero config values resolve to the defaults shown, in place. */
typedef struct {
    /* --- Config --- */

    uint64_t seed;         /* random root; same seed + config = same cave */
    float fill_prob;       /* initial floor probability per cell, in [0,1];
                              0 -> 0.45; higher = more open caves */
    int birth_low, birth_high;      /* wall -> floor if floor neighbors (8-conn,
                                       0..8) in [low, high]; 0,0 -> 4,5 */
    int survive_low, survive_high;  /* floor stays if neighbors in [low, high];
                                       0,0 -> 4,8 */
    int iterations;        /* smoothing passes; 0 -> 5; more = smoother, slower */
    int connect;           /* > 0: carve corridors connecting all floor regions
                              (O(N^2*W^2*H^2), N = regions; keep 0 on large grids);
                              0: skip (not resolved from 0) */
    void (*write)(uint64_t seed, const void *data, int x, int y,
                  rlk_tile_t *out);  /* per-cell writer (required), see rlk_ca_fill */
    const void *write_data; /* opaque user pointer passed to write; NULL ok */

    /* --- Buffers (caller-owned, must outlive the generator; w*h of the
           fill window) --- */

    uint8_t *grid;         /* cell states: 0 = wall, 1 = floor */
    int *region_ids;       /* multi-use scratch: double-buffer during iterations,
                              compact region labels (0 = wall, 1..N) after */

    /* --- Runtime (filled by rlk_ca_fill) --- */

    int win_w, win_h;      /* window geometry, cells */
    int region_count;      /* floor regions after fill: 0 = none, 1 = connected,
                              >1 = disconnected */
} rlk_ca_t;

/* Resolve zero defaults and validate (safety asserts).
   Call once after filling the config fields. */
void rlk_ca_init(rlk_ca_t *ca);

/* Generate (seed, iterate, connect) and render onto tm over the window
   `win`: a world rect of w x h cells of `cellsize` tiles. Buffers
   grid/region_ids must hold w*h elements of that window.
   The tilemap must fully cover the window (asserted).

   Rendering: the writer is called once per cell, then the produced tile is
   stamped over the cell's cellsize x cellsize tile block, except:
     - cells with mask weight 0 are not written (mask NULL = all allowed;
       the mask geometry must equal `win`);
     - tiles inside the bbox of a POI of `poi` are not written (reserved
       areas, poi NULL = none).

   spawn (NULL = ignored): on output, the entrance POI — anchor on the
   floor cell nearest the centre of the largest floor region (the bbox
   centre itself may be rock), bbox = the region's world rect, seed =
   rlk_poi_seed(ca->seed, ...). type/gen/level are left as the caller set
   them. Not written if there is no floor region.

   Not const: mutates ca->grid, ca->region_ids and the runtime fields. */
void rlk_ca_fill(rlk_ca_t *ca, const rlk_lattice_t *win, rlk_tilemap_t *tm,
                 const rlk_mask_t *mask, const rlk_poi_list_t *poi,
                 rlk_poi_t *spawn);

#endif /* RLK_CA_H */
