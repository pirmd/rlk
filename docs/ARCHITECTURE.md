# Architecture decisions

The key decisions distilled from the design discussion. These are
**binding** for implementation; deviations require updating this file.

## 1. Determinism vs. mutable state

A region splits cleanly into two layers:

- **`RegionGenData`** — deterministic, fully derivable from the world seed +
  region coordinates. Never persisted. Can be regenerated bit-identically at
  any time (the `Tilemap`, base `occ_mask`, initial scatter positions, POI
  macro placements).
- **`RegionSaveState`** — the mutable overlay, persisted. Everything that
  changes after generation and cannot be re-derived: harvested `ResourceNode`
  stages, `occ_mask` harvest deltas, spawner `current_population`, live
  `EntityStore` slots.

Load rule: **`loaded = regenerated ⊕ RegionSaveState`**. Regeneration is the
base; the save is a delta applied on top.

### Why this split exists

If everything were persisted, saves would balloon and break on any layout
change. If nothing were persisted, revisiting a region would regenerate a
"clean" state and silently drop player progress (harvested bushes, depleted
spawners). The split keeps saves minimal and version-resilient.

### The `occ_mask` trap

`occ_mask` was conceived as derivable from the seed. But harvesting a mature
tree mutates solidity — what was meant to be deterministic becomes mutable.
Decision: `occ_mask` is computed at load as `generated XOR harvested`, where
`harvested` is a persisted delta in `RegionSaveState`. This keeps the
deterministic base pure and makes mutation explicit. (See Phase 1.)

### Serialization rule

`RegionSaveState` serializes **only the minimal mutable delta**, never raw
buffers. Consequence for `EntityStore`: persist only live slots
`(id, components)`, not the internal carved buffer — insulates saves from
`entity_store_required_size` layout changes across versions.

---

## 2. Vegetation ≠ animals (do not unify)

| | Vegetation | Animals |
|---|---|---|
| Nature | static data + growth state | dynamic ECS entities |
| Placement | scatter at generation (deterministic) | runtime spawner (from POI) |
| Interaction | explicit action (harvest) | collision dispatch (hunt) |
| Evolution | regrowth timer | population cap + respawn |
| Persistence | mutable overlay on region | position ephemeral; spawner persisted |

Forcing a bush into the full ECS (collider, transform, AI systems) is
over-engineering: a bush has no velocity, no AI, no behavior — just a
position, type, and growth stage. A flat `ResourceLayer` array beside the
`Tilemap` suffices. Conversely, a rabbit moves, flees, and behaves — it
needs the full ECS. Matching the model to the element avoids sur-architecture
in both directions.

### Vegetation specifics

- `ResourceLayer` = flat array of `ResourceNode { type, stage, x, y,
  regrowth_timer, loot_seed }`, allocated alongside the region `Tilemap`.
- Placement reuses the density/scatter machinery (Phase 2): one density
  field per `ResourceType`, weighted Poisson-disk. Mature trees **carve into
  `occ_mask`** (solid); berry bushes stay non-solid (visual + interaction).
- Harvest is an **explicit action** ("harvest"), not passive collision:
  query nearby nodes (like `entity_grid` but on `ResourceLayer`), apply to
  the nearest mature one. Start with O(count) scan; move to a spatial grid
  only if node counts grow.
- Regrowth tick runs on an **episodic cadence (≤ 1/s, not per-frame)** and
  iterates only non-mature nodes — the per-frame full scan is the real
  scaling risk, not the occasional harvest query.
- `loot_seed` per node gives deterministic loot, but only if loot depends on
  the node alone. If loot depends on global state (luck, bonuses), either
  keep it purely seed-determined or persist the rolled result. **Pick one
  and document** (Phase 5).

### Animal specifics

- `POI_RABBIT_WARREN` / `POI_BEAR_DEN` are **active spawners**, not décor.
  `SpawnerState { den_type, species, x, y, population_cap,
  current_population, respawn_timer, roam_radius }`.
- `AnimalTag` is a real ECS entity: `LAYER_ANIMAL` collider + `spawner_id`
  so death decrements `current_population` — this is what makes the
  ecosystem feel finite rather than an infinite game stream.
- Lifecycle: spawn near the den within `roam_radius` → roam/flee → killed
  (loot + decrement) → respawn when timer elapses and under cap.
- **Animals are ephemeral on region unload** (recommended): only
  `SpawnerState` persists. An animal that wandered past a region border is
  destroyed on unload; the spawner repopulates on reload. Simpler, no
  cross-region entity migration.
- V1 AI: `WANDER`/`FLEE`. Flee = vector away from player, tested against
  `tilemap_is_solid_circle`. **No A\* pathfinding** for fleeing small game —
  a flee vector is credible. Predators (bears) need approach/hunt behavior
  → `AIState` shared state machine, Phase 7 (optional, only if V1 proves
  insufficient).

---

## 3. Interaction dispatch

`handler[LAYER_A][LAYER_B]` table, static, compile-time-fixed `LAYER_COUNT`.
Registered once at startup, resolved on collision/overlap. First real use
case: `LAYER_PROJECTILE` vs `LAYER_ANIMAL` (hunt) — the arrow→target pattern.
Harvest is **not** a dispatch case: it is an explicit action, not passive
overlap.

The table is **static memory** and an explicit exception to the allocation
convention (see ALLOCATION.md). It re-enters the convention only if
`LAYER_COUNT` becomes dynamic or multiple worlds need distinct tables —
neither is a V1 need.

---

## 4. Integration boundary with pxl

rlk is rendering-agnostic. [pxl](https://github.com/pirmd/pxl) is the
visualization layer. The boundary is strict and non-negotiable:

- **rlk never includes `pxl.h`**, never opens a window, never draws.
- **pxl never includes rlk internals**, only the public `rlk.h`.
- The `demo/` module bridges the two: reads rlk structures (`Tilemap`,
  `ResourceLayer`, entities) and draws them via pxl. The bridge is trivial
  because rlk structures are flat data; pxl just iterates and draws.
- **rlk must not be modeled for easy drawing** (no colors, no sprite frames
  stored in `ResourceNode`). The demo translates; rlk stays pure data/logic.
- The library builds and tests **headless** (no pxl). `demo/` is an opt-in
  build target requiring SDL2 or X11 and is **not** part of `make test`.

pxl is also a **debug/validation tool, not just decoration**: visualize the
density field as a heatmap + Poisson-disk points + `occ_mask` overlay
(Phase 2); draw growth stages and trigger harvest on click; use pxl's
`stepper.time_scale` to fast-forward regrowth/respawn timers (Phase 5/6).

### Honesty rule for the demo

A demo that regenerates from the seed **lies** once mutations exist: it
shows a clean state, hiding harvested nodes and depleted spawners.
Therefore the demo must load `regenerated ⊕ RegionSaveState`, never
regeneration alone. This is why Phase 1 (RegionSaveState) must precede any
vegetation/animal demo. The Phase 0 demo (raw `Tilemap`) may use plain
regeneration, since no mutation exists yet.
