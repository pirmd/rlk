# RLK - Naming conventions

1. Module: file `<module>.h`, identifiers `rlk_<module>_*`, types
   `rlk_<module>_t`, macros `RLK_<MODULE>_*`. A domain prefix exists
   only for a family of modules that only make sense together: `wg_`
   groups the worldgen pipeline, i.e. the engine (`wg.h`, `rlk_wg_t`,
   `rlk_wg_fill`) and its stages/bridges (`wg_relief`, `wg_drain`,
   ...). A module usable without its domain gets a generic name
   (`ca`, `bsp`, `path`, `hydro` -- not `wg_hydro`: it is terrain).
2. Verbs (one meaning each):
   - `init`: prepares a struct; `build`: computes derived data;
   - `find`: a search that can fail; `compute`: computes a result into
     a caller-provided output;
   - `sample` / `at`: read; `weight`, `allows`: mask query;
   - `fill`: generates content into a tilemap region (generators);
   - `draw`: draws an analysis result into a tilemap;
   - `push`: append to a bounded list; `add`: register a stage;
   - `remove`: removes a property (e.g. depressions, `remove_pits`).
3. Argument order: computing functions -> output first (`out`, `dst`,
   `m`), like `memcpy`. Tilemap writes -> `(gen, tm, ...)` for `fill`,
   `(tm, source, ...)` for `draw`.
4. Coordinates, positions and regions:
   - `x, y` = world tiles (int); `i, j` = cell indices;
   - `wx, wy` = world coords when `x, y` are already taken (e.g. POI);
   - `px, py` = continuous position (float, e.g. noise sampling) or
     screen pixel in viewers; `cx, cy` = cell center (float);
   - `x0, y0, x1, y1` = segment endpoints; `mx, my` = mouse pixel;
   - any region, window or footprint is an `rlk_rect_t` (`x, y, w, h`).
5. Buffer sizes: in number of elements of the type documented by the
   module, never in bytes.
6. Return: `rlk_err_t` if the call can fail, else `void` or a value.
