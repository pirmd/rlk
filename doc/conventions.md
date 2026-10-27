# RLK - Naming conventions

1. Module: file `<module>.h`, identifiers `rlk_<module>_*`, types
   `rlk_<module>_t`, macros `RLK_<MODULE>_*`. The `wg_` prefix is
   reserved for the worldgen pipeline and its bridges/stages (`wg_nf`,
   `wg_flow`, `wg_ca`, ...). For everything else ask: "is it usable
   without worldgen?" If yes, generic name.
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
