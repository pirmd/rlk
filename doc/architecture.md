# RLK - Layered architecture

This document describes the **target architecture** of the library, from
core to applications. Each layer is optional on top of the previous one:
you can stop at any level and build your own variant. The current code is
being migrated to this architecture; the `TODO` proposes the migration
plan.

Elements marked **[+]** are additions relative to the current code.

Naming rules are in `doc/conventions.md`; the memory policy ("Memory
model") and the determinism contract are in this document; the GIS/DEM
glossary is in `TODO` (bullet `demo_dump.h`).

`rlk.h` remains the umbrella header (includes everything); including
individual headers remains the granular path. Its alignment with the
layers and its completeness (`wg_nf_relief.h`, `wg_nf_rivers.h` are
missing) are tracked in the `TODO`.

---

## Overview

```
Layer 5   Applications     demos, games, tools
Layer 4   Generators       bsp, ca                                   [worldgen]
Layer 3   Pipeline         wg_nf + bridges (grid, drain) + stages    [worldgen]
Layer 2   Grid algorithms
             terrain :      slope, drain
             spatial :      path, fov (planned)
Layer 1   Base : data      geom, grid, mask, poi, tilemap
Layer 0   Base : core      err, hash, fmath, noise, rng
```

Rules:

1. A layer depends on lower layers. Within a layer, dependencies are
   explicit in the module table and acyclic. Each module declares its
   layer and domain at the top of its file (`/* layer: 2, domain: spatial */`)
   and a CI script checks the `#include`s.
2. Everything that flows between modules is **caller-owned POD** (grid,
   mask, POI list, tilemap). No allocation in the lib (details: "Memory
   model" below).
3. **rlk base (layers 0-1)**: no mention of worldgen. These types are
   consumed by all domains, including ecs, lighting and rendering.
4. **Independent domains**: within layer 2, one domain does not depend on
   the other. Layers 3-4 consume layer 2 only through their bridges
   (e.g. `wg_nf_drain`). A program can embed only `path` + `fov`, or only
   `wg_nf`. Anything serving several domains lives in the base
   (e.g. `rlk_tilemap_line`).
5. Generators (layer 4) only know base types.
6. **Module split criterion.** A module exists separately if one of these
   holds:
   - it has at least two unrelated consumers;
   - it belongs to another domain or has other dependencies;
   - it is useful on its own (without the neighboring algorithm).
   Otherwise its functions join the module of their only consumer and
   stay public. Merging headers costs nothing in binary as long as the
   `.c` files stay separate: the linker only pulls the objects used.

### Bounding contract

- Base and `wg_nf` alone: pure functions of world coordinates -> infinite
  world or chunk-streamed.
- `drain`: **bounded global grid**, precomputed, must cover the whole area.
- `path` and `fov`: computation limited to a **window** (`rlk_lattice_t`)
  provided by the caller; compatible with streamed regions.

---

## Determinism contract

Same seed + same build -> same world, byte for byte.

- Discrete decisions (fill order, tie-breaks, A*) on quantized integers,
  never on "almost equal" floats.
- Golden tests in CI: hash of the generated tilemap for a few fixed seeds.
- Build flags are enforced in `Makefile.inc` (no `-ffast-math`,
  `-ffp-contract=off`).

---

## Memory model: zero allocation

The lib never allocates: no `malloc`/`calloc`/`realloc`/`free` in the
`.c`/`.h`. Four mechanisms:

- Caller-owned pointer: world-sized data lives outside the structs
  (`rlk_tilemap_t.tiles`, `rlk_grid_t.v`, `rlk_drain_t.{filled,dir,acc,order}`,
  `rlk_ca_t.{grid,region_ids}`). The caller chooses static, stack or heap.
- Scratch as a parameter: algorithmic work is materialized in a buffer
  provided by the caller (`rlk_drain_build(..., scratch)`).
- Buffer reuse: a caller buffer can serve as internal scratch (e.g.
  `region_ids` as a double-buffer during CA iterations); the aliasing is
  documented in a comment.
- Compile-time-capped embedded arrays: config only, never world data
  (`rlk_wg_nf_t.fields[16]`/`stages[8]`/`zones[16]`, modulator
  `bias[16]`/`gain[16]`; `ctx.field[16]` is per-tile state, on the stack).

Rules:

- R1: no embedded array sized by world data in a lib struct.
  Compile-time cap (config) only.
- R2: world-sized data -> caller-owned pointer.
- R3: algorithmic scratch -> explicit parameter.
- R4: buffer aliasing allowed, documented.
- R5 (sole exception to R1): `rlk_wg_poi_list_t.items[RLK_WG_POI_MAX]`
  embeds the POI list; migrating to `rlk_poi_list_t { *v, n, cap }`
  would remove it. Until then, R1 holds at 100% for everything else.

On the caller side (demos, tests): big buffers at file scope `static`,
small fixtures on the stack; the lib does not decide storage.

### Sizing helper policy

The lib allocating nothing, its only helpers are sizing helpers:

- H1 (no allocation helper): no `rlk_*_alloc`, no implicit `free`.
  Every buffer belongs to the caller.
- H2 (sizes in elements, never bytes): the type is the one documented by
  the module (`uint64_t scratch[W * H]` for `drain`); the multiplication
  in `size_t` when not a compile-time constant.
- H3 (no scratch macro): caller-owned buffers are declared directly in
  elements (`w*h` per output buffer, type documented on the struct).
- H4 (compile-time caps `RLK_<MODULE>_MAX_*`): fixed-cap arrays
  (`RLK_WG_NF_MAX_FIELDS/ZONES/STAGES/AXES`, `RLK_WG_BSP_MAX_NODES`,
  `RLK_WG_POI_MAX`) or window limits (`RLK_WG_FLOW_MAX_CELLS`).
- H5 (validation by `assert`): a too-small buffer is a programmer
  error: `assert(buf_sz >= ...)` (e.g. `rlk_tilemap_init`), never a
  runtime error code.

---

## Layer 0 - Base: core

| Module | Role |
|--------|------|
| `err.h` | Error codes (`rlk_err_t`, `rlk_err_to_string`, `RLK_OK`, `RLK_E_INVALID`, [+] `RLK_E_NOTFOUND`) |
| `hash.h` | Deterministic 64-bit hashing (`rlk_hash`, `rlk_hash_sub`) |
| `fmath.h` | Maths: `rlk_clampf`, `rlk_smoothstep`, `rlk_lerp`, `rlk_remap` |
| `noise.h` | Noise: Perlin, FBM, ridged, domain warp |
| `rng.h` | Deterministic PRNG (xorshift) |

`RLK_E_NOTFOUND` [+]: "no result", distinct from an invalid argument
(used by `rlk_path_find`).

---

## Layer 1 - Base: data

2D structures shared by all domains. POD, caller-owned.
Header-only (`static inline` functions).

| Module | Role | Consumers |
|--------|------|-----------|
| `geom.h` [+] | `rlk_rect_t` (region) + `rlk_lattice_t` (lattice over the world) | grid, mask, drain, path, fov, poi, tilemap, demos, lighting, spatial hash |
| `grid.h` (ex `wg_grid.h`) | `rlk_grid_t`: float raster, bilinear sampling | terrain, lighting, odors, influence |
| `mask.h` [+] | `rlk_mask_t`: `uint8` raster (weights, density, visibility) + constructors | worldgen, fov, fog of war, passability |
| `poi.h` (ex `wg_poi.h`) | `rlk_poi_t`, `rlk_poi_list_t`: points of interest | worldgen, ecs spawners, region regen |
| `tilemap.h` | `rlk_tilemap_t`, `rlk_tile_t`, [+] `rlk_tile_props_t`, [+] `rlk_tilemap_line` | everything |

### geom.h [+]

Two cross-cutting types: `rlk_rect_t` (region / window / footprint) and
`rlk_lattice_t` (lattice laid over the world). Same layout as pxl rects
(`x, y, w, h`) but distinct types, so that rlk does not depend on pxl;
the conversion happens in the application.

```c
typedef struct { int x, y, w, h; } rlk_rect_t;

static inline int  rlk_rect_contains(const rlk_rect_t *r, int x, int y);
static inline int  rlk_rect_overlaps(const rlk_rect_t *a, const rlk_rect_t *b);
/* 1 and *out = intersection if non-empty, else 0 */
static inline int  rlk_rect_intersect(rlk_rect_t *out, const rlk_rect_t *a,
                                      const rlk_rect_t *b);
```

A single tile <-> cell conversion, shared by every module that lays a
lattice over the world. Fields follow GIS raster vocabulary
(`cellsize`). With `cellsize = 1`, a `lattice` is a tile-aligned window.

```c
typedef struct {
    int w, h;        /* dimensions in cells */
    int cellsize;    /* cell side, in tiles */
    int origin_x, origin_y;  /* world origin, in tiles */
} rlk_lattice_t;

/* Linear index (j * w + i) of the cell containing tile (x, y),
   or -1 if outside the grid. */
static inline int rlk_lattice_index(const rlk_lattice_t *g, int x, int y);

static inline int rlk_lattice_same(const rlk_lattice_t *a, const rlk_lattice_t *b);

/* Tile-aligned window (cellsize = 1) covering the rect */
static inline rlk_lattice_t rlk_lattice_from_rect(const rlk_rect_t *r);
```

### grid.h (ex `wg_grid.h`)

Single-band float raster. **One single type** for all quantities: an
elevation grid (DEM) has no dedicated type, it is named `elev`.
Quantities without a vertical dimension (temperature, light, odor) use
the same type.

```c
typedef struct { rlk_lattice_t lattice; float *v; } rlk_grid_t;

/* Bilinear value at world position (x, y) */
static inline float rlk_grid_sample(const rlk_grid_t *g, float x, float y);
```

Example: `rlk_grid_t elev = { .lattice = { .w = GW, .h = GH, .cellsize = 4, .origin_x = -192, .origin_y = -192 }, .v = elev_buf };`

### mask.h [+]

`uint8` raster: 0 = excluded, 1..255 = weight (255 = full). Depending on
usage: placement density (worldgen), visibility (`fov`), passability,
fog of war. Depends on `geom`, `grid` and `tilemap`.

```c
typedef struct { rlk_lattice_t lattice; uint8_t *v; } rlk_mask_t;

/* Weight at world position (x, y); 0 outside the grid */
static inline int rlk_mask_weight(const rlk_mask_t *m, int x, int y);
static inline int rlk_mask_allows(const rlk_mask_t *m, int x, int y);

/* Copy the geometry of a grid; buf (w*h bytes) provided by the caller */
static inline void rlk_mask_init_like(rlk_mask_t *m, const rlk_grid_t *g,
                                      uint8_t *buf);

/* Hard threshold: 255 if lo <= g[c] <= hi, else 0. Identical geometries required. */
static inline rlk_err_t rlk_mask_threshold(rlk_mask_t *m, const rlk_grid_t *g,
                                           float lo, float hi);

/* Ramp: weight 0 at v0, 255 at v1, linear between (v1 < v0 allowed) */
static inline rlk_err_t rlk_mask_ramp(rlk_mask_t *m, const rlk_grid_t *g,
                                      float v0, float v1);

/* AND: dst[c] = min(dst[c], src[c]). RLK_E_INVALID if geometries differ. */
static inline rlk_err_t rlk_mask_and(rlk_mask_t *dst, const rlk_mask_t *src);

/* Mask from a tilemap: 255 if props->flags[tile.type] has bit `flag`, else 0.
   m->lattice must match rlk_tilemap_lattice(tm) (cellsize 1).
   region: sub-rect to recompute (incremental update), NULL = all. */
static inline rlk_err_t rlk_mask_from_tilemap(rlk_mask_t *m,
                                              const rlk_tilemap_t *tm,
                                              const rlk_tile_props_t *props,
                                              uint8_t flag,
                                              const rlk_rect_t *region);
```

### tilemap.h: tiles, properties and drawing

```c
typedef struct {
    uint8_t type;     /* opaque index (defined by the game) */
    uint8_t flags;    /* free bits for the game */
    uint8_t biome;    /* continuity zone */
    uint8_t variant;  /* variation within type/biome */
    uint8_t depth;    /* [+] depth (water, underground): 0 = surface */
} rlk_tile_t;

typedef struct {
    int         w, h;    /* dimensions in tiles */
    int         origin_x, origin_y;  /* [+] world origin, in tiles */
    rlk_tile_t *tiles;   /* w*h tiles, caller-provided */
} rlk_tilemap_t;

/* [+] Write a tile at world position (x, y). No effect if outside the tilemap. */
static inline void rlk_tilemap_set(rlk_tilemap_t *tm, int x, int y,
                                   rlk_tile_t tile);
```

`rlk_tilemap_init` (buffer size assert) and `rlk_tilemap_get` from the
current code: keep or merge with `set` — undecided, see `TODO`.

`rlk_tile_t.type` is a plain index. What "walkable" or "transparent"
means is game data, not lib data: a table provided by the caller,
indexed by `type`.

```c
enum {
    RLK_TILE_WALKABLE    = 1 << 0,
    RLK_TILE_TRANSPARENT = 1 << 1
    /* following bits reserved for the application */
};

typedef struct {
    const uint8_t *flags;   /* ntypes entries */
    int            ntypes;
} rlk_tile_props_t;

/* Tilemap geometry: w, h, cellsize 1, tilemap origin */
static inline rlk_lattice_t rlk_tilemap_lattice(const rlk_tilemap_t *tm);

/* Draw a segment from (x0,y0) to (x1,y1), square brush of `width` tiles
   centered on the line (width >= 1). Clipped to the tilemap. Deterministic. */
static inline void rlk_tilemap_line(rlk_tilemap_t *tm, int x0, int y0,
                                    int x1, int y1, int width, rlk_tile_t tile);
```

Flags are **positive** (no `SOLID` or `OPAQUE`): the resulting mask is
directly an "allowed" mask. A `type` outside the table has no flag
(neither walkable nor transparent), as a precaution.

`fov` and `path` therefore never read the tilemap directly: they consume
masks (or callbacks). The rebuild cost is one pass per tile, and only on
the modified region during a dynamic change (door, felled tree).

`rlk_tilemap_line` is the base drawing primitive: `rlk_path_draw` builds
on it, and it also serves for hand-placed walls or roads.

### poi.h (ex `wg_poi.h`)

A gameplay type as much as a generation type: worldgen creates them,
animal spawners (ecs) attach to them, and region regen restarts from
`seed`, `type` and `bbox`.

```c
typedef struct {
    int        x, y;      /* world anchor (e.g. entrance), in tiles */
    rlk_rect_t bbox;      /* world footprint, in tiles (contains (x,y)) */
    uint32_t   type;      /* same role as rlk_tile_t.type (distinct types) */
    uint64_t   seed;      /* POI-local seed (derived via rlk_hash_sub) */
    uint8_t    gen;       /* generation only: 0 = placed by wg_nf; +1 per POI
                             emitted by a generator. Meaningless at runtime. */
    uint8_t    level;     /* target level/map (0 = overworld) */
} rlk_poi_t;

typedef struct { rlk_poi_t *v; int n, cap; } rlk_poi_list_t;

/* RLK_E_INVALID if the list is full */
static inline rlk_err_t rlk_poi_push(rlk_poi_list_t *l, const rlk_poi_t *p);
```

**Identity**: the index in a persistent `rlk_poi_list_t` is the stable
identifier of a POI. Such a list is never reordered or compacted; ecs
spawners reference a POI by this index.

Overlap rule: a generator only writes inside the `bbox` of its POI. If
two `bbox`es overlap (`rlk_rect_overlaps`), the first POI processed wins
and the second is ignored; the processing order is deterministic
(emission order).

---

## Layer 2 - Grid algorithms

Computations over base types. Two independent domains, four modules.

### Terrain domain

Terrain analysis and hydrology, on any elevation field.

| Module | Role | Depends on |
|--------|------|-----------|
| `slope.h` [+] | Slope from an elevation grid. Header-only. | `grid` |
| `drain.h/c` (ex `wg_flow.h/c`) | Pits, D8 drainage, accumulation, distance to river network, accumulation mask | `grid`, `mask` |

#### slope.h [+]

```c
/* Slope in rise/run ratio at cell (i, j), Horn 3x3 method.
   zscale: height, in tiles, of one elevation unit of the grid.
   Rise is computed in elevation units x zscale, run in tiles (cellsize). */
static inline float rlk_slope_at(const rlk_grid_t *g, int i, int j,
                                 float zscale);
```

Without `zscale`, a normalized elevation (0..1) over multi-tile cells
would give almost-always-zero slopes, and a slope threshold would mean
nothing. Example: normalized elevation representing 128 tiles of relief
-> `zscale = 128.0f`. Same role as gdaldem slope's `-s` option.

The parameter is on the function, **not in `rlk_grid_t`**: grids without
a vertical dimension have no `zscale`. `aspect` and `hillshade` stay in
the demos (rendering).

`slope` remains a separate module: it has three unrelated consumers
(`path` cost, `poi` stage, demos), and folding it into `grid.h` would
bring a terrain notion into the base.

#### drain.h/c (ex `wg_flow.h/c`)

Full hydrology of an elevation field. **Bounded global grid required.**
(Named `drain` so as not to be confused with pathfinding "flow fields".)
The module groups three public functions; pits are integrated because
`rlk_drain_build` is their only consumer: Priority-Flood dominates its
cost, and calling it alone to avoid D8 and accumulation saves almost
nothing.

```c
typedef struct {
    rlk_lattice_t lattice;
    uint16_t  *filled;  /* [+] filled elevation, quantized (w*h) */
    uint8_t   *dir;     /* downstream D8 direction (0..7), RLK_DRAIN_OUTLET=8 */
    uint32_t  *acc;     /* drainage area in cells (w*h) */
    int       *order;   /* topological order: downstream before upstream (w*h) */
} rlk_drain_t;

#define RLK_DRAIN_OUTLET     8     /* dir: drains out of the domain */
#define RLK_DRAIN_SCRATCH(w, h)  ((size_t)(w) * (size_t)(h))
/* number of uint64_t elements to provide in scratch (remove_pits and build) */

/* Pit removal only [+]. out: w*h quantized uint16_t (pit-free elevation).
   (GIS: "fill sinks"; `pitremove` in TauDEM.) */
rlk_err_t rlk_drain_remove_pits(uint16_t *out, const rlk_grid_t *elev,
                                float sea, uint64_t *scratch);

/* Full drainage: fills filled, dir (D8), acc, order.
   POD usage:
     rlk_drain_t drain = { .lattice = elev.lattice, .filled = filled_buf,
                           .dir = dir_buf, .acc = acc_buf, .order = order_buf };
     rlk_drain_build(&drain, &elev, sea, scratch); */
rlk_err_t rlk_drain_build(rlk_drain_t *d, const rlk_grid_t *elev, float sea,
                          uint64_t *scratch);

/* Signed distance [+], in tiles, from tile (x, y) to the river network:
   < 0 in the channel, 0 on the bank, > 0 outside. RLK_DRAIN_FAR if no
   network segment is nearby. Pure function of (d, x, y): no allocation,
   no side effect, evaluable tile by tile in any order (streamable). */
#define RLK_DRAIN_FAR  1.0e9f
float rlk_drain_river_sdf(const rlk_drain_t *d, int x, int y, uint32_t acc_min);

/* Mask from accumulation: 255 if lo <= acc[c] <= hi, else 0.
   Mask geometry = drain geometry. */
rlk_err_t rlk_drain_acc_mask(rlk_mask_t *m, const rlk_drain_t *d,
                             uint32_t lo, uint32_t hi);
```

Definition of `rlk_drain_river_sdf`:
- the network is the set of segments "cell center -> downstream cell
  center (`d->dir`)" for cells with accumulation >= `acc_min`;
- segment half-width = `0.5 * min(1 + log2(acc / acc_min), cellsize)`
  tiles;
- only the segments of the 3x3 cells around the one containing the tile
  are tested (at most 9 segments): constant cost per tile;
- consequence: a channel is never wider than `cellsize` tiles
  (open question: configurable 5x5 range, see `TODO`).

The returned value is a distance, not a boolean: banks, beaches and
estuaries are then set by continuous thresholds, which a binary mask
cannot express.

### Spatial domain

Grid queries for gameplay as much as for generation. No dependency on
terrain or worldgen.

| Module | Role | Depends on |
|--------|------|-----------|
| `path.h/c` [+] | A* path: costs as array or callback, bounded window, drawing | `geom`, `tilemap` |
| `fov.h/c` [+] *(planned)* | Field of view / visibility from a point | `mask` |

`path` and `fov` remain separate: they only share `geom` and `mask`, and
have distinct algorithms, users and tests.

#### path.h/c [+]

8-connected A* over **integer** costs (determinism). Two forms, same
engine:

- **array** (`rlk_path_find`): precomputed costs; suited to roads over a
  coarse grid;
- **callback** (`rlk_path_find_fn`): cost evaluated on demand; suited to
  in-game navigation, where tiles change and entities block.

```c
typedef struct {
    rlk_lattice_t lattice;
    uint16_t  *v;     /* w*h: 0 = impassable, 1..65535 = crossing cost */
} rlk_path_cost_t;

/* Cost of the cell containing tile (x, y) - any cellsize.
   0 = impassable. Must be deterministic for a given state. */
typedef uint16_t (*rlk_path_cost_fn)(void *user, int x, int y);

#define RLK_PATH_SCRATCH(w, h)  ((size_t)3 * (size_t)(w) * (size_t)(h))
/* number of uint32_t elements to provide in scratch (cumulative cost, parent, heap) */

/* Array form. */
rlk_err_t rlk_path_find(int *out, int cap, int *n,
                        const rlk_path_cost_t *c,
                        int x0, int y0, int x1, int y1,
                        uint32_t *scratch);

/* Callback form. window bounds the search (scratch is sized by
   window->w x window->h): typically rlk_lattice_from_rect(&area) around
   the start. */
rlk_err_t rlk_path_find_fn(int *out, int cap, int *n,
                           const rlk_lattice_t *window,
                           rlk_path_cost_fn cost, void *user,
                           int x0, int y0, int x1, int y1,
                           uint32_t *scratch);

/* Draw a path: segments between the centers of cells cells[0..n-1] of
   lattice (typically the output of rlk_path_find), via rlk_tilemap_line. */
rlk_err_t rlk_path_draw(rlk_tilemap_t *tm, const rlk_lattice_t *lattice,
                        const int *cells, int n, int width, rlk_tile_t tile);
```

In both search forms, `out` receives cell indices (`rlk_lattice_index`
of the grid or of the window), start -> end; `*n` = length. Returns:
- `RLK_OK`;
- `RLK_E_NOTFOUND`: no path within the window;
- `RLK_E_INVALID`: endpoint outside the window or impassable, or `cap`
  too small.

Details:
- Diagonals: cost x sqrt(2), approximated in integers (181/128).
- Heuristic: octile distance at minimum cost 1 (admissible).
- Deterministic tie-breaks: neighbors visited in a fixed order, heap ties
  resolved by the lowest cell index.
- Cumulative cost saturates at `UINT32_MAX`.

*(Possible evolution, not included: `rlk_path_distance`, multi-source
distance map for "nearest POI".)*

#### fov.h/c [+] (planned, API to validate)

Sketch, to check that the base is enough: `fov` consumes and produces
masks, just as `path` consumes costs.

```c
/* visible receives 255 / 0; transparent is a cellsize-1 mask (typically
   rlk_mask_from_tilemap(..., RLK_TILE_TRANSPARENT, ...)). The geometry of
   visible can be a window around (x, y). */
rlk_err_t rlk_fov_compute(rlk_mask_t *visible, const rlk_mask_t *transparent,
                          int x, int y, int radius);
```

---

## Layer 3 - The `wg_nf` pipeline (field-based generation) [worldgen]

Per-tile engine, pure in world coordinates. Its bridges and stages keep
the `wg_` prefix.

### Internal pipeline

```
sampled fields -> modulation (provinces) -> derive (hook)
        -> classify (biomes) -> paint (tile)
```

Derived fields (`ctx->field[]`) act as a **data bus** between stages.
Bridges fill this bus from external sources.

### Unified stage API

Bridges and stages expose the same shape: a function that turns a config
into an `rlk_wg_nf_stage_t { fn, data }`, and a single `rlk_wg_nf_add`.
Stages run in registration order: bridges first.
The modulator only acts on sampled fields: register it before the stages
that consume them (relief, rivers, poi).

```c
rlk_err_t rlk_wg_nf_add(rlk_wg_nf_t *wg, rlk_wg_nf_stage_t stage);
/* RLK_E_INVALID if the stage array is full */

rlk_wg_nf_stage_t rlk_wg_nf_modulator(const rlk_wg_nf_modulator_cfg_t *cfg);
rlk_wg_nf_stage_t rlk_wg_nf_grid  (const rlk_wg_nf_grid_cfg_t   *cfg);
rlk_wg_nf_stage_t rlk_wg_nf_drain (const rlk_wg_nf_drain_cfg_t  *cfg);
rlk_wg_nf_stage_t rlk_wg_nf_relief(const rlk_wg_nf_relief_cfg_t *cfg);
rlk_wg_nf_stage_t rlk_wg_nf_rivers(const rlk_wg_nf_rivers_cfg_t *cfg);
rlk_wg_nf_stage_t rlk_wg_nf_poi   (const rlk_wg_nf_poi_cfg_t    *cfg);

rlk_err_t rlk_wg_nf_fill(rlk_wg_nf_t *wg, rlk_tilemap_t *tm, int x, int y);
```

A cfg is a **branching POD**: `const` pointers to caller-owned data,
never mutated by the lib — zero defaults are resolved inside the stage,
not by an `init` that would mutate the config. Canonical example (the
smallest bridge):

```c
typedef struct {
    const rlk_grid_t *grid;  /* pre-filled; must survive until fill */
    uint8_t slot;            /* destination derived field */
    float scale;             /* 0 -> 1 */
    float offset;
} rlk_wg_nf_grid_cfg_t;
```

| Module | Role | Depends on |
|--------|------|-----------|
| `wg_nf.h/c` | Engine: fields, modulation, derive, classify, paint, `add`, `fill` | — |
| `wg_nf_modulator.h/c` | Voronoi modulation of sampled fields (provinces) | — |
| `wg_nf_grid.h/c` | Bridge: samples an `rlk_grid_t` -> derived field | `grid` |
| `wg_nf_drain.h/c` (ex `wg_nf_flow.h/c`) | Bridge: samples an `rlk_drain_t` -> fields (acc, dir, lake) | `drain` |
| `wg_nf_relief.h/c` | Stage: standardized elevation | via fields |
| `wg_nf_rivers.h/c` | Stage: rivers, pluggable sources (`river_fn`); lakes, noise streams, width normalized by gradient, estuary | `drain` (drainage source, optional) |
| `wg_nf_poi.h/c` | Stage: POI detection, writes into an `rlk_poi_list_t` | `drain` optional |

```
grid  --> wg_nf_grid  --> ctx->field[D_X]   --> consumer stage
drain --> wg_nf_drain --> ctx->field[D_ACC] --> consumer stage --> wg_nf_poi
```

---

## Rivers: a single geometry

Two approaches coexisted for placing the rivers of an `rlk_drain_t` on a
tilemap: a `wg_nf_rivers` stage (per tile, in the pipeline) and a `draw`
function (pass over a filled tilemap). The stage does everything `draw`
did, and more:

| | `wg_nf_rivers` (stage) | `draw` (removed) |
|---|---|---|
| Evaluation | per tile, pure in world coordinates; streamable | pass over an already-filled tilemap |
| Sources | pluggable (`river_fn`): drainage, noise | drainage only |
| Width | normalized by the field gradient | logarithm of accumulation |
| Lakes, noise streams | yes, optional | no |
| Estuary, beach | estuary rule (mudflats) | none |
| Biomes | feeds `classify` / `paint` | overwrites their result |
| Usable without `wg_nf` | no | yes |

**Decision: remove `draw`, merge the geometry into `rlk_drain_river_sdf`.**
- `rlk_drain_river_sdf` (`drain.h`) carries the network geometry: signed
  distance per tile, pure, no tilemap dependency.
- `wg_nf_rivers`, for a drainage source, calls this function and adds what
  belongs to the pipeline: lakes, noise streams, gradient-normalized
  width, estuary rule.
- A user without `wg_nf` calls it directly in a loop (layer 2): the same
  geometry, without the engine.

Consequence: `drain` does not depend on `tilemap`; `rlk_tilemap_line`
remains (for `rlk_path_draw` and manual drawing); the verb `draw` is
reserved for paths (`rlk_path_draw`).

---

## Layer 4 - Procedural generators [worldgen]

Standalone generators (base only), usable without `wg_nf`. Their content
is generic; they stay in the worldgen domain by usage.

| Module | Role |
|--------|------|
| `ca.h/c` (ex `wg_ca.h/c`) | Cellular automata (caves) |
| `bsp.h/c` (ex `wg_bsp.h/c`) | Binary partition (dungeons, rooms) |

Common contract, identical for both:

```c
rlk_err_t rlk_bsp_fill(rlk_bsp_t *g, rlk_tilemap_t *tm,
                       const rlk_poi_t *poi, rlk_poi_list_t *spawn);
rlk_err_t rlk_ca_fill (rlk_ca_t  *g, rlk_tilemap_t *tm,
                       const rlk_poi_t *poi, rlk_poi_list_t *spawn);
```

- Writes only inside `poi->bbox`, with seed `poi->seed`.
- `g->mask` (`const rlk_mask_t *`, NULL = no constraint) restricts
  placement.
- `spawn` (NULL allowed) receives newly emitted POIs (`gen + 1`): a cave
  can call a dungeon, a dungeon a sub-cave.

---

## Composition: the "POI -> generators" pattern (not a layer)

No `wg_world` module for now: the pattern fits in one loop.
The POI list serves as a work queue that grows during iteration; zero
allocation, size bounded by `cap`.

```c
rlk_poi_t qbuf[256];
rlk_poi_list_t q = { qbuf, 0, 256 };
rlk_wg_nf_fill(&wg, &tm_world, 0, 0);      /* terrain + initial POIs into q */

for (int i = 0; i < q.n; i++) {            /* q.n may grow during the loop */
    rlk_poi_t p = q.v[i];
    if (p.gen > MAX_GEN) continue;
    if (!rlk_mask_allows(&allowed, p.x, p.y)) continue;
    rlk_tilemap_t *tm = level_map[p.level];  /* one tilemap per level: caller's choice */
    switch (p.type) {
    case POI_DUNGEON: rlk_bsp_fill(&bsp, tm, &p, &q); break;
    case POI_CAVE:    rlk_ca_fill (&ca,  tm, &p, &q); break;
    }
}
```

**Roads between POIs**: after the loop, for each pair of neighboring
POIs, `rlk_path_find` then `rlk_path_draw` (see level 2b). The pair order
(e.g. minimum spanning tree over the overworld POIs) is chosen by the
caller.

**Promotion to `wg_world.h/c`**: only when two projects rewrite this loop
with a `type -> rule` table (generator + mask). It would then take the
`wg_` prefix, because it composes a world.

---

## Layer 5 - Applications

| Module | Role |
|--------|------|
| `[+] demo_viewer.c` | Generic PXL viewer: sources `world` (e.g. `demo_tilemap_viewer`: relief + rivers + drainage + POI, aspect/hillshade in rendering), `ca`, `bsp`, `nav`; field modes via stage probe |
| `[+] viewer.h` | Shared PXL boilerplate (window, camera, layout, minimap, HUD, font) — interactive tool, outside the lib |
| `[+] demo_world.c` | The POI -> generators pattern, with roads |
| `[+] demo_nav.c` | In-game navigation: passability mask, `path_find_fn` in a window |
| `demo_fantasy.c` | Fantasy generation in terminal |
| `demo_dungeon.c` | Dungeon generation in terminal |
| `demo_cave.c` | Cave generation in terminal |
| `[+] demo_dump.h` | Debug export of a grid (see below) |

### demo_dump.h (debug, outside the lib)

Export of an `rlk_grid_t` or `rlk_mask_t` to inspect it outside the
program. Header-only, in the demos: the lib never writes files.

```c
/* ESRI ASCII grid: opens in QGIS, GDAL (gdalinfo, gdaldem) */
int demo_dump_asc(const char *path, const rlk_grid_t *g);

/* 8-bit PGM, values normalized between lo and hi: visible in any image viewer */
int demo_dump_pgm(const char *path, const rlk_grid_t *g, float lo, float hi);
int demo_dump_mask_pgm(const char *path, const rlk_mask_t *m);
```

Mapping with the ASCII grid header: `ncols = w`, `nrows = h`,
`cellsize = cellsize`, `xulcorner = origin_x`. Beware of row order: the
format expects the **first row at the north** (y increasing upward). If
the tilemap y axis points down, write rows in order `j = 0..h-1` and set
`yulcorner = -(origin_y + h * cellsize)`: the image appears upright in
QGIS, with flipped ordinates. To be validated once against the real
tilemap convention.

---

## Usage levels

### Level 1 - Standalone bricks (base + terrain)

"I just want a slope, pit removal or drainage."

```c
#include "grid.h"
#include "slope.h"   /* or drain.h */

float elev_buf[256 * 256];
rlk_grid_t elev = { .lattice = { .w = 256, .h = 256, .cellsize = 8, .origin_x = 0, .origin_y = 0 }, .v = elev_buf };
/* fill elev with normalized 0..1 noise, representing 128 tiles of relief */
float s = rlk_slope_at(&elev, 128, 128, 128.0f);
```

### Level 2 - Rivers on a tilemap without `wg_nf` (base + terrain)

"I want rivers on my tilemap."

```c
#include "drain.h"

rlk_grid_t elev = { .lattice = { .w = GW, .h = GH, .cellsize = 4, .origin_x = -192, .origin_y = -192 }, .v = elev_buf };
rlk_drain_t drain = { .lattice = elev.lattice, .filled = f_filled, .dir = f_dir,
                      .acc = f_acc, .order = f_order };
rlk_drain_build(&drain, &elev, SEA_LEVEL, f_scratch);

for (int y = TM_Y0; y < TM_Y0 + TM_H; y++)
    for (int x = TM_X0; x < TM_X0 + TM_W; x++)
        if (rlk_drain_river_sdf(&drain, x, y, RIVER_THRESHOLD) <= 0.0f)
            rlk_tilemap_set(&tm, x, y, (rlk_tile_t){ .type = TILE_RIVER });
```

This is the same query that `wg_nf_rivers` calls. For lakes, streams and
estuaries, go through the pipeline (level 3).

### Level 2b - Road between two points (base + terrain + spatial)

"I want to connect two villages avoiding water and steep slopes."

```c
#include "slope.h"
#include "path.h"

#define ELEV_ZSCALE 128.0f   /* elevation 0..1 = 128 tiles of relief */

/* Integer cost per cell: 0 = water, else 1 + weighted slope */
static uint16_t cost_buf[GW * GH];
rlk_path_cost_t cost = { .lattice = elev.lattice, .v = cost_buf };
for (int j = 0; j < GH; j++)
    for (int i = 0; i < GW; i++)
        cost_buf[j * GW + i] = elev.v[j * GW + i] < SEA_LEVEL ? 0
            : (uint16_t)rlk_clampf(
                  1.0f + 20.0f * rlk_slope_at(&elev, i, j, ELEV_ZSCALE),
                  1.0f, 65535.0f);

static uint32_t path_scratch[RLK_PATH_SCRATCH(GW, GH)];
int cells[512], n;
if (rlk_path_find(cells, 512, &n, &cost, vx0, vy0, vx1, vy1, path_scratch) == RLK_OK)
    rlk_path_draw(&tm, &cost.lattice, cells, n, 1, (rlk_tile_t){ .type = TILE_ROAD });
```

### Level 2c - In-game navigation (base + spatial)

"The player clicks: I want a path that avoids walls and entities."

```c
#include "mask.h"
#include "path.h"

/* Tile type properties: game data */
static const uint8_t tile_flags[N_TILE_TYPES] = {
    [TILE_GRASS] = RLK_TILE_WALKABLE | RLK_TILE_TRANSPARENT,
    [TILE_WALL]  = 0,
    [TILE_WATER] = RLK_TILE_TRANSPARENT,
    /* ... */
};
rlk_tile_props_t props = { .flags = tile_flags, .ntypes = N_TILE_TYPES };

/* Passability mask of the region, computed once */
static uint8_t walk_buf[REGION_W * REGION_H];
rlk_mask_t walkable = { .lattice = rlk_tilemap_lattice(&tm), .v = walk_buf };
rlk_mask_from_tilemap(&walkable, &tm, &props, RLK_TILE_WALKABLE, NULL);

/* A door opens: refresh only its tile */
rlk_rect_t dirty = { door_x, door_y, 1, 1 };
rlk_mask_from_tilemap(&walkable, &tm, &props, RLK_TILE_WALKABLE, &dirty);

/* Dynamic cost: terrain + blocking entities */
typedef struct { const rlk_mask_t *walk; const void *ecs; } nav_t;
static uint16_t nav_cost(void *user, int x, int y) {
    const nav_t *nav = user;
    if (!rlk_mask_allows(nav->walk, x, y)) return 0;
    return entity_blocks(nav->ecs, x, y) ? 0 : 1;
}

/* Search bounded to 64x64 tiles around the player */
rlk_rect_t area = { px - 32, py - 32, 64, 64 };
rlk_lattice_t win = rlk_lattice_from_rect(&area);
static uint32_t nav_scratch[RLK_PATH_SCRATCH(64, 64)];
nav_t nav = { &walkable, ecs };
int cells[256], n;
rlk_err_t e = rlk_path_find_fn(cells, 256, &n, &win, nav_cost, &nav,
                               px, py, tx, ty, nav_scratch);
```

### Level 3 - Full `wg_nf` pipeline (base + worldgen)

"I want relief + rivers + lakes + POIs, automatically."

```c
#include "wg_nf.h"
#include "wg_nf_grid.h"
#include "wg_nf_drain.h"
#include "wg_nf_relief.h"
#include "wg_nf_rivers.h"
#include "wg_nf_poi.h"

rlk_wg_nf_t wg;
rlk_wg_nf_init(&wg, seed, nsampled, nderived);

/* Configs must survive until rlk_wg_nf_fill */
rlk_wg_nf_grid_cfg_t  temp_cfg  = { .grid = &temp_grid, .slot = D_TEMP };
rlk_wg_nf_drain_cfg_t drain_cfg = { .drain = &drain, .elev = &elev, .slot_acc = D_ACC };
/* relief_cfg, rivers_cfg, poi_cfg: see wg_nf_relief.h, wg_nf_rivers.h, wg_nf_poi.h */

/* Bridges first (they fill the bus), then the stages */
rlk_wg_nf_add(&wg, rlk_wg_nf_grid(&temp_cfg));
rlk_wg_nf_add(&wg, rlk_wg_nf_drain(&drain_cfg));
rlk_wg_nf_add(&wg, rlk_wg_nf_relief(&relief_cfg));
rlk_wg_nf_add(&wg, rlk_wg_nf_rivers(&rivers_cfg));
rlk_wg_nf_add(&wg, rlk_wg_nf_poi(&poi_cfg));

rlk_wg_nf_fill(&wg, &tm, 0, 0);
```

### Level 4 - Composition (base + worldgen + spatial)

"I want the terrain POIs to trigger dungeons and caves, connected by
roads."
Level 3 + `bsp` / `ca` + `path` + the loop pattern above.

---

## Gap with the current code

This document describes the target architecture. The list below
summarizes the main gaps with the current code; the `TODO` details the
migration plan.

- Grid: `wg_grid.h`, `rlk_wg_grid_t {w,h,cell,ox,oy,v}` -> `grid.h`, `rlk_grid_t {rlk_lattice_t lattice; float *v}`
- Geometry: flat fields in `rlk_wg_grid_t` -> `geom.h`, `rlk_lattice_t {w,h,cellsize,origin_x,origin_y}`
- Region: — -> `geom.h`, `rlk_rect_t {x,y,w,h}`
- Drainage: `wg_flow.h/c`, `rlk_wg_flow_t {w,h,lo,built,fill,dir,acc,order}` -> `drain.h/c`, `rlk_drain_t {lattice,filled,dir,acc,order}`
- Drain bridge: `wg_nf_flow.h`, `rlk_wg_nf_flow_add()` -> `wg_nf_drain.h`, `rlk_wg_nf_add(&wg, rlk_wg_nf_drain(&cfg))`
- Slope: — -> `slope.h`, `rlk_slope_at(g, i, j, zscale)`
- Mask: — -> `mask.h`, `rlk_mask_t` + constructors
- Path: — -> `path.h/c`, `rlk_path_find`, `rlk_path_find_fn`, `rlk_path_draw`
- FOV: — -> `fov.h/c` (planned)
- POI: `wg_poi.h`, `rlk_wg_poi_t {wx,wy,kind,seed,size,flags,data[8]}` -> `poi.h`, `rlk_poi_t {x,y,bbox,type,seed,gen,level}`
- POI list: `rlk_wg_poi_list_t {items[256], count}` -> `rlk_poi_list_t {*v, n, cap}`
- Tilemap: `rlk_tilemap_t {width,height,tiles}`, `rlk_tile_t {type,flags,biome,variant}` -> `[+] origin_x,origin_y` in tilemap; `[+] depth` in tile; `[+] rlk_tilemap_set`
- Tile props: — -> `[+] rlk_tile_props_t`, flags `RLK_TILE_WALKABLE/TRANSPARENT`
- Drawing: — -> `[+] rlk_tilemap_lattice`, `rlk_tilemap_line`
- Generators: `wg_ca.h`, `wg_bsp.h` (`wg_` prefix) -> `ca.h/c`, `bsp.h/c` (generic prefix)
- Generator contract: `rlk_wg_ca_fill(ca, ox, oy, tm)` -> `rlk_ca_fill(g, tm, poi, spawn)` + `mask`
- wg_nf API: six `rlk_wg_nf_<x>_add()` -> `rlk_wg_nf_add(&wg, rlk_wg_nf_<x>(&cfg))`
- Error: `RLK_OK`, `RLK_E_INVALID`, `RLK_E_RANGE` (never used) -> `[+] RLK_E_NOTFOUND`, removal of `RLK_E_RANGE`
- Rivers: `wg_nf_rivers` with internal geometry -> `[+] rlk_drain_river_sdf` (shared geometry)

### Documentation and CI

- `rlk.h`: umbrella aligned with the layers and completed (stages `relief`, `rivers` missing); README follows
- Golden tests (tilemap hash for fixed seeds; paths for fixed grids)
- `RLK_*_SCRATCH` macros in each concerned module
- CI script: checks `#include`s per layer and domain independence

---

## Analogy with pxl

| pxl | rlk |
|-----|-----|
| Core (`pxl.h`): window, events, input | Base (layers 0-1): `err`, `hash`, `fmath`, `noise`, `rng`, `geom`, `grid`, `mask`, `poi`, `tilemap` |
| Optionals (`app.h`, `layout.h`, `ui.h`) | Domains: terrain (`slope`, `drain`), spatial (`path`, `fov`), worldgen (`wg_nf`, `ca`, `bsp`) |

The user picks their base then their domains: the base alone is enough
for a custom variant, each domain adds on without pulling the others.
