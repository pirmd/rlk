# rlk

Minimaliste C library for rogue-like or 2D-grid based games.

**Status:** work in progress — design phase. No code yet; this repository
currently holds design documents (`docs/`) and a phased TODO (`TODO.md`).
Implementation follows the roadmap in [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Goals

rlk provides the deterministic world layer of a rogue-like: seeded world
generation (macro world map + regional tilemaps), static resources (flora),
dynamic entities (fauna) via a minimal ECS, an interaction dispatch, and
persistence of mutable state across region reloads. It is intentionally
**agnostic of rendering**: rlk never opens a window or draws a pixel.

The companion project [pxl](https://github.com/pirmd/pxl) (C99 2D graphics
library) is the intended visualization layer. The boundary is strict:
**rlk never includes `pxl.h`**; the demo that bridges the two lives in a
separate build target.

## Core design principles

1. **No hidden allocation.** Every memory need is exposed up front
   (`_required_size`, `_scratch_size`, capacity parameters). The library
   never calls `malloc`/`calloc`/`realloc`/`free` internally. See
   [`docs/ALLOCATION.md`](docs/ALLOCATION.md).
2. **Determinism first.** World and region generation is reproducible from a
   seed. What is derivable from the seed is never persisted — only the
   *mutable* overlay is saved. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
3. **Static vs. dynamic separation.** Vegetation = static data with growth
   state (a flat array beside the `Tilemap`). Animals = full ECS entities
   spawned from persisted POI spawners. Do not force one model onto the other.
4. **Primitives without policy.** Like pxl, rlk exposes opaque operations and
   leaves strategy (memory backing, loop policy, save store) to the user.

## Documentation

- [`TODO.md`](TODO.md) — phased checklist, current status, next steps.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — phases 0–7, scope, dependencies,
  verification criteria.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — key design decisions:
  `RegionGenData` vs `RegionSaveState`, vegetation vs animals, interaction
  dispatch, integration boundary with pxl.
- [`docs/ALLOCATION.md`](docs/ALLOCATION.md) — the three allocation patterns
  (A/B/C), error codes, failure convention, static-dispatch exception.

## Layout (target)

```
rl/
  world/    Tilemap, ResourceLayer, ECS, entity_grid, dispatch
  gen/      worldgen, mapgen, poi
  persist/  RegionGenData / RegionSaveState serialization
rlk.h       umbrella public header (aggregates modules)
docs/       design documents
test/       headless unit tests (no pxl dependency)
demo/       optional pxl-based visualization demo (separate build target)
```

The library builds and tests **headless** (no pxl). `demo/` is an opt-in target
requiring SDL2 or X11 and is not part of `make test`.

## License

MIT. See [LICENSE](LICENSE).
