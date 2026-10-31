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
             worldgen  pipeline: wg + stages (modulator, grid, drain, hydro,
                       relief, rivers, poi)
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
  |           |          |          └ axes (<=3) -> zone boxes -> zone, zone2, depth, edge
  |           |          └ stages via rlk_wg_add, registration order
  |           └ derived slots written into ctx->field[]
  └ nsampled noise fields (FBM/RIDGE) sampled into ctx->field[]
```

Sampling convention: the fields of tile `(wx, wy)` are sampled at the tile
centre (`(float)wx + 0.5f, (float)wy + 0.5f`), consistent with
`rlk_grid_get` / `rlk_grid_center`. Stages that re-sample a field
(`rlk_wg_sample_field`) follow the same convention.

Each stage declares the slots it reads and writes (`rlk_wg_stage_t.reads` /
`.writes`, bitmasks over the sampled fields and the derived slots).
`rlk_wg_init()` validates the declarations in registration order and returns
`RLK_E_INVALID` if a stage reads a derived slot that no earlier stage writes
(or references a slot out of range; same check for the axes, against all
stages). Stage order is therefore explicit and checked, not implicit.

Drivers (sinks are pluggable; the tilemap is only one sink):

- `rlk_wg_run(wg, ox, oy, w, h, sink, user)` evaluates the pipeline over the
  world rect `(ox, oy, w, h)` and calls `sink(ctx, user)` per tile, row-major.
- `rlk_wg_fill(wg, ox, oy, tm)` is the tilemap sink over `rlk_wg_run`;
  chunked generation is just repeated calls with different origins.
- To fill a grid with a sampled field (macro -> micro driver, e.g. the
  elevation grid a drain runs on), sample `rlk_wg_sample_field` at each
  cell centre (`rlk_grid_center`, so `cellsize > 1` samples at the true
  cell centre) -- see demo/tilemap_gen.c.

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
