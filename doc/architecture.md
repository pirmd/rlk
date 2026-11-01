# RLK - Architecture

## Structure

Three zones, read bottom-up. You can stop at any level: base only, base +
one domain, several domains:
. Within the base, `data` may use `core` and other `data` modules
  (acyclic).
. Domains are independent: no cross-domain include. Anything serving
  several domains lives in the base (`rlk_tilemap_set_line`).
. Everything that flows between modules is caller-owned POD (grid, mask,
  POI list, tilemap). No allocation in the lib (see "Memory model").

```
 USER LAND   your game, your tools, your composition code
             (examples: demo/, recipes below -- never included by rlk)
 ---------------------------------------------------------------
 DOMAINS     terrain   slope; drain (filled/dir/acc/order from an elevation
                       grid); hydro (slope/strahler/speed per cell, from a
                       built drain) — chain: elev grid -> drain -> hydro.
                       drain and hydro stay separate layers: hydro is
                       optional above drain (wg_rivers reads drain only), and
                       both are end-user API because the wg stages take
                       pre-built drain/hydro pointers (two-level generation).
             nav       path
             vision    fov
             worldgen  pipeline: wg + stages (region = macro-areas from Voronoi
                       cells, drain/hydro = readers of pre-built grids,
                       rivers = river mask + carve, relief = elevation,
                       poi = detectors)
                       structure generators: ca, bsp (windowed, POI-driven)
 ---------------------------------------------------------------
 BASE        data      geom, grid, mask, poi, tilemap
             core      err, hash, fmath, noise
```

## world generation pipeline

Per-tile engine, pure in world coordinates `(wx, wy)`:

```
(wx, wy) -> FIELDS -> STAGES -> CLASSIFY -> WRITE -> tile
  |           |          |          |          └ zone writer (ctx, data, &tile)
  |           |          |          └ axes (<=3) -> zone boxes -> zone, zone2, inside, border
  |           |          └ stages via rlk_wg_add, registration order
  |           └ derived slots written into ctx->field[]
  └ nsampled noise fields (FBM/RIDGE) sampled into ctx->field[]
```

Sampling convention: the fields of tile `(wx, wy)` are sampled at the tile
centre (`(float)wx + 0.5f, (float)wy + 0.5f`), consistent with
`rlk_grid_get` / `rlk_grid_center`. Stages that re-sample a field
(`rlk_wg_sample_field`) follow the same convention.

Stage order is the registration order and is a contract, not checked: a
stage may only read derived slots written by stages registered before it,
and an axis may only read slots written by some stage. Register consumers
after their producers (e.g. wg_relief after wg_rivers for the carve slot).

Custom stages are user land and typically ~10 lines. Recipe: inject a
pre-filled grid into a derived slot (scale/offset) — this is how an external
raster enters the pipeline:

```c
static void my_grid_stage(rlk_wg_ctx_t *ctx, const void *data) {
    const struct { const rlk_grid_t *grid; uint8_t slot; float scale, offset; } *c = data;
    ctx->field[ctx->wg->nsampled + c->slot] =
        rlk_grid_get(c->grid, (float)ctx->wx + 0.5f, (float)ctx->wy + 0.5f) * c->scale + c->offset;
}
```

Generation:

- `rlk_wg_fill(wg, ox, oy, tm)` evaluates the pipeline over the tilemap's
  world rect, row-major, and writes each tile (chunked generation is
  repeated calls with the tilemap origin set per chunk).
- To fill a grid with a sampled field (macro -> micro, e.g. the elevation
  grid a drain runs on), call `rlk_wg_fill_grid(wg, field, &grid)` after
  `rlk_wg_init`: each cell gets the field sampled at its centre
  (`rlk_grid_center`, so `cellsize > 1` samples at the true cell centre).
  Then compose the macro products yourself in user land, e.g.
  `rlk_drain_build(&drain, &grid, sea, scratch)`.

### Composing stages (recipes)

Stages are optional layers; compose only what you need. `wg_relief` alone
already gives a relief to classify on — each extra stage refines it:

- `wg_relief` only: standardised elevation from sampled fields (warp,
  hills, mountains, fjords, lapse) into a derived slot. No macro build
  needed.
- + `wg_region` before it: macro-areas (Voronoi regions) biasing the sampled
  fields, so biomes read as coherent provinces.
- + `wg_drain` reader: exposes a pre-built `rlk_drain_t` (acc, dir, fill
  depth) as derived slots. Macro build: `rlk_wg_fill_grid` an elevation
  field into a coarse grid, then `rlk_drain_build` (after `rlk_wg_init`,
  before the first `rlk_wg_fill`).
- + `wg_hydro` reader: exposes a pre-built `rlk_hydro_t` (slope, Strahler
  order, speed). Needs `-> rlk_hydro_build` on top of the drain.
- + `wg_rivers` after the drain reader, before `wg_relief`: river mask +
  valley carve from the drain SDF; `wg_relief` then reads the carve slot
  (`river_carve_field`, registration order matters).
- + `wg_poi`: fills the POI list during the fill (work queue for the local
  level, see below).

Rules of thumb:

- A reader stage (`wg_drain`, `wg_hydro`) is worth it when several
  consumers (writers + stages) read its slots; with a single consumer,
  that consumer reads the source POD directly (e.g. `wg_poi`'s `min_acc`
  reads `drain->acc`, `wg_rivers` computes log10(acc) itself).
- Macro builds (grid -> drain -> hydro) happen once, outside the wg
  pipeline, on a coarse lattice; the pipeline stages just read them.

### Boilerplate: full pipeline with hydrology

Order contract: fill the wg config -> `rlk_wg_init` (seeds the noise) ->
macro build (after init) -> register stages (before the first fill) ->
`rlk_wg_fill`. All buffers caller-owned, no allocation:

```c
/* caller-owned buffers */
float    elev_buf[GW*GH];
uint16_t drain_filled[GW*GH];  uint8_t drain_dir[GW*GH];
uint32_t drain_acc[GW*GH];     int drain_order[GW*GH];
uint64_t scratch[GW*GH];
rlk_tile_t tiles[TM_W*TM_H];

rlk_wg_t wg = {0};
wg.seed = 42;  wg.nsampled = 1;
wg.fields[0] = (rlk_wg_field_t){ .kind = RLK_WG_FBM, .freq = 0.005f, .salt = 1 };
/* axes, zones, inside_scale, border_width ... */
rlk_wg_init(&wg);

/* macro: project field 0 onto a coarse grid, build the drain on it */
rlk_grid_t elev = { .lattice = { .w = GW, .h = GH, .cellsize = 4 }, .values = elev_buf };
rlk_wg_fill_grid(&wg, 0, &elev);
rlk_drain_t drain = { .lattice = elev.lattice, .filled = drain_filled, .dir = drain_dir,
                      .acc = drain_acc, .order = drain_order };
rlk_drain_build(&drain, &elev, SEA_LEVEL, scratch);

/* stages: readers take the pre-built PODs; register before the first fill */
rlk_wg_drain_cfg_t dcfg = { .drain = &drain, .elev = &elev,
                            .slot_acc = 0, .slot_dir = 1, .slot_fill = 2 };
rlk_wg_add(&wg, rlk_wg_drain(&dcfg));
/* ... rlk_wg_rivers, rlk_wg_relief, custom stages ... */

rlk_tilemap_t tm;
rlk_tilemap_init(&tm, TM_W, TM_H, tiles, sizeof(tiles));
rlk_wg_fill(&wg, 0, 0, &tm);
```

The macro grid is fixed per wg (same lattice as the world rect it covers);
tiles outside it read default slot values. `rlk_wg_fill_grid` works for any
field, not just elevation — the link field -> grid -> drain is user land.

## Two-level generation

The wg pipeline is the macro level: per-tile, pure in `(seed, wx, wy)`,
it produces the tilemap AND the POI list (the `wg_poi` stage). Local content
is a second, separate level: a user-land loop over the POI list runs windowed
generators (`ca`, `bsp`) that stamp their output into the reserved POI
bboxes, then post passes (roads, cosmetics). The POI list is the work queue
between the two levels.

`ca`/`bsp` are NOT wg stages and are not meant to be: a stage sees one tile
at a time and only edits `ctx->field[]`, while `ca`/`bsp` generate over a
whole window and stamp `cellsize x cellsize` tile blocks into a tilemap.
Keeping them out of the pipeline keeps the pipeline pure and chunkable;
the composition stays in user land (recipe below).

## Memory model

No allocation in the lib: every buffer is caller-owned POD passed by pointer
(grid/mask/tilemap/POI buffers, scratch arrays). Functions either fill a
caller buffer or write through a caller sink; the only runtime checks are
system-allocation failures, programmer errors are `assert`s.
---

## User land: composition recipes

The library composes nothing for you. The POI -> generators pattern fits in
one loop (executable reference: `demo/demo_world.c`); the POI list is the
work queue, growing during iteration, zero allocation:

```c
rlk_rect_t map_r = { 0, 0, MAP_W, MAP_H }, box;
for (int i = 0; i < pois.n; i++) {          /* pois.n may grow */
    const rlk_poi_t *p = &pois.pois[i];
    /* window = bbox clamped to the map (cellsize 1), user-land idiom: */
    if (!rlk_rect_intersect(&box, &p->bbox, &map_r)) continue;
    rlk_lattice_t win = { .w = box.w, .h = box.h, .cellsize = 1,
                          .origin_x = box.x, .origin_y = box.y };
    if (p->gen > MAX_GEN) continue;
    switch (p->type) {
    case POI_CAVE:    rlk_ca_fill (&ca,  &win, tm, NULL, &done, &spawn); break;
    case POI_DUNGEON: rlk_bsp_fill(&bsp, &win, tm, NULL, &done, &spawn); break;
    }
    rlk_poi_push(&done, p);   /* reserve the bbox */
}
/* roads: rlk_path_find + rlk_path_draw between neighbouring POIs */
```

## Extending rlk results (game-defined fields)

The lib's records are fixed compact PODs whose fields are game-defined and
opaque to rlk. Extension points, by data density:

- Sparse per-instance data (structures, quests): the POI list. The index in
  a persistent `rlk_poi_list_t` is the stable id of a POI, so a parallel
  caller array `game_poi[cap]` indexed by that id carries any extra state.
- Per-tile semantics: `rlk_tile_props_t` (per-type flags table) consumed by
  `rlk_mask_from_tilemap`. The tile's own 5-byte record (`type`, `flags`,
  `biome`, `variant`, `depth`) is the game's per-tile attribute.
- Dense per-tile game data (ownership, fertility, cost): a caller-owned side
  grid over the same lattice (demo: `cost_buf`, `poi_res`), not a field of
  the tile -- the tile stays 5 bytes and cache-friendly.
- Behaviour: callbacks with an opaque `void *data` (wg zone writers, ca/bsp
  `write`, path cost `fn`). Pure per-tile variation is derived from the
  deterministic hash (seed, x, y) into `variant`, not stored.

Demos (examples, not part of the lib): `demo_viewer.c` (PXL terrain viewer
over the `tilemap_gen` package: `tilemap_gen.c` + `gen_stages/gen_write/
gen_post/gen_stats.c`), `demo_world.c` (full world: overworld + POI
structures + roads, ASCII views of the generation stages via argv).
