# RLK - Naming conventions

1. Module: file `<module>.h`, identifiers `rlk_<module>_*`, types
   `rlk_<module>_t`, macros `RLK_<MODULE>_*`. A domain prefix exists
   only for a family of modules that only make sense together: `wg_`
   groups the worldgen pipeline, i.e. the engine (`wg.h`, `rlk_wg_t`,
   `rlk_wg_fill`) and its stages/bridges (`wg_relief`, `wg_drain`,
   ...). A module usable without its domain gets a generic name
   (`ca`, `bsp`, `path`, `hydro` -- not `wg_hydro`: it is terrain).
   Structs are anonymous typedefs (`typedef struct { ... } rlk_x_t;`).
   A named tag (`struct rlk_x_s` + forward declaration) is used only
   when a type must be referenced before its full definition (e.g.
   `rlk_wg_ctx_t` in the `rlk_wg_stage_t` callback, wg.h); nowhere else.
2. Verbs (one meaning each):
   - `init`: prepares a struct; `build`: computes derived data;
   - `find`: a search that can fail; `compute`: computes a result into
     a caller-provided output;
   - `get`: read at a position (value or pointer; tilemap, grid, mask,
     slope); `allows`: mask query;
   - `fill`: generates content into a tilemap region (generators);
   - `draw`: draws an analysis result into a tilemap;
   - `push`: append to a bounded list; `add`: register a stage;
   - `remove`: removes a property (e.g. depressions, `remove_pits`).
3. Argument order: computing functions -> output first (`out`, `dst`,
   `m`), like `memcpy`. Tilemap writes -> `(gen, tm, ...)` for `fill`,
   `(tm, source, ...)` for `draw`. `from_` names a function that fills a
   caller-provided buffer (`rlk_mask_from_tilemap`); a small POD derivation
   is returned by value instead (`rlk_tilemap_lattice`, `rlk_grid_center`).
4. Coordinates, positions and regions:
   - `x, y` = world tiles (int); `i, j` = cell indices;
   - `wx, wy` = world coords when `x, y` are already taken (e.g. POI);
   - `wxf, wyf` = float world position, typically the tile centre
     (`wxf == (float)wx + 0.5f`); noise sampling at a tile in wg stages
     uses it (int lookups and hashing use `wx, wy`);
   - `px, py` = continuous position (float, e.g. noise sampling) or
     screen pixel in viewers; `cx, cy` = cell center (float);
   - `x0, y0, x1, y1` = segment endpoints; `mx, my` = mouse pixel;
   - any region, window or footprint is an `rlk_rect_t` (`x, y, w, h`).
5. Buffer sizes: in number of elements of the type documented by the
   module, never in bytes.
6. Return: `rlk_err_t` if the call can fail, else `void` or a value.
7. POD buffer fields are named after their element (`tiles`, `values`,
   `weights`, `pois`, `acc`), never a bare `v`.
8. Suffix `f` marks a float variant only when the bare name would be
   ambiguous with a non-float sibling (`rlk_clampf`, `rlk_dist_euclideanf`
   vs `rlk_dist_manhattan`, `rlk_hash_normf` vs a norm/magnitude). A float
   function with no ambiguous sibling carries no `f` (`rlk_lerp`,
   `rlk_wg_rand`, `rlk_grid_get`). "Normalized to [0,1]" is `norm`.
9. Interval notation: `[0,1)` for hash/uniform draws (the max draw is < 1
   by construction, e.g. rlk_hash_normf), `[0,1]` for normalized fields and
   masks where 1 is attainable (river centre, acc == w*h, smoothstep at
   t = 1). Value noise and fBm inherit `[0,1)` from the lattice hashes.
   Both `[a,b)` and the French-style `]a,b[` are valid per ISO 80000-2, but
   comments are English and ASCII here: always `[a,b)`, never `]a,b[`.
10. Randomness: `seed` is the root of a random source (a world, a noise
    generator, a structure, a POI: `rlk_wg_t.seed`, `rlk_noise_t.seed`,
    `rlk_poi_t.seed`). `salt` is a decorrelating value mixed into an
    existing seed to distinguish several uses or instances of the same
    source: parameters of `rlk_hash_sub` / `rlk_hash_at` / `rlk_wg_rand`,
    and cfg fields such as `rlk_wg_field_t.salt`,
    `rlk_wg_region_cfg_t.salt`, `rlk_wg_poi_cfg_t.salt`.
