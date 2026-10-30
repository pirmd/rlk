# RLK - Architecture

## Structure

Three zones, read bottom-up. You can stop at any level: base only, base +
one domain, several domains:
. Within the base, `data` may use `core` and other `data` modules
  (acyclic).
. Domains are independent: no cross-domain include. Anything serving
  several domains lives in the base (`rlk_tilemap_line`).
. Everything that flows between modules is caller-owned POD (grid, mask,
  POI list, tilemap). No allocation in the lib ("Memory model" below).

```
 USER LAND   your game, your tools, your composition code
             (examples: demo/, recipes below -- never included by rlk)
 ---------------------------------------------------------------
 DOMAINS     terrain   slope, drain, hydro
 (independent) nav     path
             vision    fov
             worldgen  wg + stages (modulator, grid, drain, hydro,
                       relief, rivers, poi); structure generators ca, bsp
 ---------------------------------------------------------------
 BASE        data      geom, grid, mask, poi, tilemap
             core      err, hash, fmath, noise, rng
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

Each stage declares the slots it reads and writes (`rlk_wg_stage_t.reads` /
`.writes`, bitmasks over the sampled fields and the derived slots).
`rlk_wg_init()` validates the declarations in registration order and returns
`RLK_E_INVALID` if a stage reads a derived slot that no earlier stage writes
(or references a slot out of range; same check for the axes, against all
stages). Stage order is therefore explicit and checked, not implicit.

Drivers (sinks are pluggable; the tilemap is only one sink):

- `rlk_wg_run(wg, ox, oy, w, h, sink, user)` evaluates the pipeline over the
  world rect `(ox, oy, w, h)` and calls `sink(ctx, user)` per tile, row-major.
- `rlk_wg_fill(wg, ox, oy, tm)` is the tilemap sink over `rlk_wg_run`.
- `rlk_wg_fill_slot(wg, slot, &grid)` is the macro -> micro driver: it fills a
  grid with `ctx->field[slot]`, the pipeline evaluated once per cell at the
  cell centre (`rlk_grid_center`, so `cellsize > 1` samples at the true cell
  centre). Typical pass: fill an elevation grid, run `drain` on it, then
  instance the micro pass on the tilemap.

---

## User land: composition recipes

The library composes nothing for you. The POI -> generators pattern fits in
one loop (executable reference: `demo/demo_world.c`); the POI list is the
work queue, growing during iteration, zero allocation:

```c
for (int i = 0; i < pois.n; i++) {          /* pois.n may grow */
    const rlk_poi_t *p = &pois.v[i];
    if (p->gen > MAX_GEN) continue;
    switch (p->type) {
    case POI_CAVE:    rlk_ca_fill (&ca,  &win, tm, NULL, &done, &spawn); break;
    case POI_DUNGEON: rlk_bsp_fill(&bsp, &win, tm, NULL, &done, &spawn); break;
    }
    rlk_poi_push(&done, p);   /* reserve the bbox */
}
/* roads: rlk_path_find + rlk_path_draw between neighbouring POIs */
```

Demos (examples, not part of the lib): `demo_viewer.c` (PXL terrain viewer
over `tilemap_gen`), `demo_world.c` (full world: overworld + POI structures
+ roads).
