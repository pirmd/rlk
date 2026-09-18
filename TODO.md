# TODO — rlk implementation roadmap

Living checklist. Branches `phase/<n>-<slug>` carry each phase's work.
Current status updates as work progresses.

## Status

- [x] Design documents (`docs/`) — foundation laid on `phase/0-socle-deterministe`.
- [~] Phase 0 — Socle déterministe (core modules + tests done; region-gen demo pending).
- [ ] Phase 1 — RegionSaveState.
- [ ] Phase 2 — Densité + scatter.
- [ ] Phase 3 — ECS + entity_grid.
- [ ] Phase 4 — Dispatch d'interaction.
- [ ] Phase 5 — Végétation.
- [ ] Phase 6 — POI spawners + animaux.
- [ ] Phase 7 — IA prédateurs (optionnel).

See [`docs/ROADMAP.md`](docs/ROADMAP.md) for scope and verification per phase.

---

## Phase 0 — Socle déterministe  `phase/0-socle-deterministe`

- [x] Foundation docs (README, TODO, ROADMAP, ARCHITECTURE, ALLOCATION).
- [x] RNG seeded (`rl/rng.h`) — splitmix64-based, reproducible streams (KAT tested).
- [x] Value noise (`rl/noise.h`) + clumping noise (biome + humidity + clumping
      density field).
- [x] `Tilemap` (`rl/world/tilemap.h`): `tilemap_required_size` + `tilemap_init`
      (Pattern A, buffer provided by caller). `Tile` struct + `occ_mask`.
- [ ] Region generation from seed; bit-identical reproducibility test. (Phase 2 territory)
- [x] `rlk_err_t` error codes (`rl/err.h`).
- [x] Build skeleton: `make` (-> test), `make lint`, `make test` (headless),
      `make demo` (pxl, opt-in, degrades gracefully if missing).
- [ ] `demo/` Phase 0': bridge rlk→pxl, draw a generated `Tilemap`. Strict
      boundary: rlk does not include `pxl.h`.

## Phase 0b — Pont pxl (démo Tilemap)  `phase/0b-pont-pxl`

Lives alongside Phase 0; first honest visualization.

- [ ] `demo/demo_tilemap.c`: mallocs visible (pxl style), feeds rlk buffers,
      renders `Tilemap` via `pxl_draw_tile`.
- [ ] Reproducibility check visually (reload seed → identical image).

## Phase 1 — RegionSaveState  `phase/1-region-savestate`

Must precede any vegetation/animal demo (otherwise the demo "lies": it
regenerates a clean state and drops mutations).

- [ ] `RegionGenData` (deterministic, derivable from seed, never persisted).
- [ ] `RegionSaveState` (mutable overlay, persisted): serialize the **delta**,
      never raw buffers. Holds: `ResourceNode` stages, `occ_mask` harvest
      deltas, spawner `current_population`, live `EntityStore` slots.
- [ ] Merge rule: `loaded = regenerated ⊕ RegionSaveState`.
- [ ] `occ_mask` strategy: `occ_mask = generated XOR harvested` (recalculate
      from determinstic base + persisted harvest deltas). Decided here because
      harvesting a tree mutates what was meant to be derivable.
- [ ] Round-trip save/load test.

## Phase 2 — Densité + scatter  `phase/2-densite-scatter`

- [ ] Density field (biome + humidity + clumping) per `ResourceType`.
- [ ] Weighted Poisson-disk scatter into `occ_mask` (mature trees = solid).
- [ ] Pattern A for result, Pattern C for scratch (flood-fill / sort).
- [ ] Tests: distribution, reproducibility, bounds respected.

## Phase 3 — ECS + entity_grid  `phase/3-ecs`

- [ ] `EntityStore`: flat per-component storage, single caller-provided buffer
      cut internally (Pattern A). `entity_store_required_size(max_entities)`.
- [ ] `entity_store_create` recycles dead slots, returns -1 if full.
- [ ] `Collider` + `pos` components, `LAYER_*` tags.
- [ ] `entity_grid` spatial index + radius query.
- [ ] **`RegionSaveState` serializes only live slots**, not the raw buffer
      (insulates saves from layout/version changes).

## Phase 4 — Dispatch d'interaction  `phase/4-dispatch`

- [ ] `handler[LAYER_A][LAYER_B]` table — **static, documented exception**
      (compile-time fixed size, no runtime alloc; see ALLOCATION.md).
- [ ] Registration + resolution.
- [ ] First real case: `LAYER_PROJECTILE` vs `LAYER_ANIMAL` (hunt).
- [ ] `roll_loot` + interaction handlers.
- [ ] Tests: collision → handler → loot.

## Phase 5 — Végétation  `phase/5-vegetation`

Assembles Phase 1 + 2 + 4.

- [ ] `ResourceLayer` + `ResourceNode` (type, stage, regrowth_timer, loot_seed).
- [ ] Placement via Phase 2 scatter; mature trees carve `occ_mask`.
- [ ] Harvest via explicit action (query near, like `entity_grid` but on
      `ResourceLayer`).
- [ ] Repousse tick: episodic cadence (≤ 1/s, not per-frame), iterate only
      non-mature nodes.
- [ ] Persist in `RegionSaveState`.
- [ ] Tranche `loot_seed`: pure deterministic (seed-node only) OR non-
      reproducible with persisted result — pick one, document.
- [ ] Demos: draw growth stages (color), harvest on click, observe regrowth
      with `pxl` stepper `time_scale` acceleration.

## Phase 6 — POI spawners + animaux  `phase/6-animaux`

Assembles Phase 3 + 4 + 1.

- [ ] `POI_RABBIT_WARREN` / `POI_BEAR_DEN` become active `SpawnerState`
      (`population_cap`, `respawn_timer`, `roam_radius`).
- [ ] `AnimalTag` (ECS, `LAYER_ANIMAL`, `spawner_id` to decrement on death).
- [ ] Lifecycle: spawn → roam/flee → killed (→ loot + decrement) → respawn.
- [ ] Decide: animals are **ephemeral on region unload** (recommended); only
      `SpawnerState` persists.
- [ ] V1 AI: `WANDER`/`FLEE`, flee = vector away from player tested against
      `tilemap_is_solid_circle`. No A* pathfinding.
- [ ] Tests: cap respected, respawn, kill→decrement, no flee through solids.

## Phase 7 — IA prédateurs (optionnel)  `phase/7-ia-predateurs`

- [ ] `AIState` shared state machine (approach/hunt for bears).
- [ ] Only if V1 proves insufficient.

---

## Conventions reminder

- Allocation patterns A/B/C — see [`docs/ALLOCATION.md`](docs/ALLOCATION.md).
- One error convention: `rlk_err_t` (codes), not `bool`, for init/generate.
- Pattern B failure convention: **atomic** (−1 = nothing written, retry with
      `estimate_count`). Harmonize `resource_layer_scatter` (currently
      "truncates silently") to the same rule.
- Dispatch table: static memory, documented exception (no `_required_size`).
