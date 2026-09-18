# Roadmap

Phased delivery. Each phase lives on a `phase/<n>-<slug>` branch and is
independently testable. Later phases assemble earlier bricks; nothing here
requires inventing new machinery beyond what the design documents.

## Dependency graph

```
Phase 0 (socle déterministe: rng, noise, Tilemap)
   ├── Phase 0b (pont pxl — démo Tilemap)         [parallèle à 0]
   └── Phase 1 (RegionSaveState)                 [conditionne toute démo honnête]
         Phase 2 (densité + scatter)               [assemble sur 0+1]
            Phase 3 (ECS + entity_grid)
               Phase 4 (dispatch d'interaction)
                  Phase 5 (végétation)             [assemble 1+2+4]
                  Phase 6 (POI spawners + animaux)  [assemble 1+3+4]
                     Phase 7 (IA prédateurs, opt.)  [seulement si V1 insuffisant]
```

---

## Phase 0 — Socle déterministe  `phase/0-socle-deterministe`

**Scope.** Foundation: RNG, noise, `Tilemap`, `rlk_err_t`, build skeleton.
Everything deterministic and reproducible from a seed.

**Deliverables.**
- `rl/err.h` — `rlk_err_t` codes.
- `rl/rng.h` — seeded RNG (splitmix64 family), reproducible streams.
- `rl/noise.h` — value noise + clumping noise (density field input).
- `rl/world/tilemap.h` — `tilemap_required_size` + `tilemap_init` (Pattern A),
  `Tile` struct, `occ_mask`.
- Region generation from seed.

**Verification.**
- Regenerate a region from the same seed → `Tilemap` bit-identical.
- `tilemap_init` with undersized buffer → returns `RLK_E_BUFSIZE`, no write.
- `make`, `make test` (headless), `make demo` (opt-in pxl, degrades if missing).

## Phase 0b — Pont pxl (démo Tilemap)  `phase/0b-pont-pxl`

**Scope.** First visualization; freezes the rlk→pxl boundary (rlk does not
include `pxl.h`). Uses plain regeneration (no mutation exists yet).

**Deliverables.** `demo/demo_tilemap.c` — visible mallocs (pxl style), feeds
rlk buffers, renders `Tilemap` via `pxl_draw_tile`.

**Verification.** Reload same seed → visually identical image.

## Phase 1 — RegionSaveState  `phase/1-region-savestate`

**Scope.** The persistence split that prevents future technical debt and
makes later demos honest.

**Deliverables.**
- `RegionGenData` (deterministic, never persisted).
- `RegionSaveState` (mutable overlay, persisted): `ResourceNode` stages,
  `occ_mask` harvest deltas, spawner `current_population`, live ECS slots.
- Merge rule `loaded = regenerated ⊕ RegionSaveState`.
- `occ_mask = generated XOR harvested` strategy.

**Verification.** Harvest a node → save → reload → state preserved (not
regenerated clean). Round-trip equality.

## Phase 2 — Densité + scatter  `phase/2-densite-scatter`

**Scope.** Density field per `ResourceType` + weighted Poisson-disk scatter.

**Deliverables.** Density field (biome + humidity + clumping), scatter into
`occ_mask` (mature trees solid). Pattern A result, Pattern C scratch.

**Verification.** Distribution sanity, reproducibility, bounds respected.
Demo: heatmap of density + scatter points + `occ_mask` overlay.

## Phase 3 — ECS + entity_grid  `phase/3-ecs`

**Scope.** Minimal ECS with fixed capacity, spatial query.

**Deliverables.** `EntityStore` (single carved buffer, Pattern A), `Collider`
+ `pos` + `LAYER_*`, `entity_grid` radius query. `entity_store_create`
recycles dead slots, −1 if full.

**Verification.** Spawn/move/query; save serializes live slots only (not raw
buffer).

## Phase 4 — Dispatch d'interaction  `phase/4-dispatch`

**Scope.** Static `handler[LAYER_A][LAYER_B]` table, registration, resolution.

**Deliverables.** Dispatch table (static, documented exception), first case
`LAYER_PROJECTILE` vs `LAYER_ANIMAL`, `roll_loot` + handlers.

**Verification.** Collision → handler → loot. Table uses no runtime alloc.

## Phase 5 — Végétation  `phase/5-vegetation`

**Scope.** Assembles Phase 1 + 2 + 4. Static resources with growth state.

**Deliverables.** `ResourceLayer` + `ResourceNode`, scatter placement,
harvest action, episodic regrowth tick, `RegionSaveState` persistence.
Pick + document `loot_seed` policy.

**Verification.** Harvest → stage advance → regrowth → persisted across
reload. Demo: stages colored, harvest on click, regrowth accelerated via
pxl `stepper.time_scale`.

## Phase 6 — POI spawners + animaux  `phase/6-animaux`

**Scope.** Assembles Phase 1 + 3 + 4. Dynamic fauna from persisted spawners.

**Deliverables.** `SpawnerState` from POI, `AnimalTag` (ECS, `spawner_id`),
lifecycle spawn→flee→kill→respawn, V1 `WANDER`/`FLEE` AI (no A*). Animals
ephemeral on region unload; only `SpawnerState` persists.

**Verification.** Cap respected, respawn under cap, kill→decrement, no flee
through solids. Demo: colliders/radii visible, population counters overlay.

## Phase 7 — IA prédateurs (optionnel)  `phase/7-ia-predateurs`

**Scope.** Only if V1 AI is insufficient for predators.

**Deliverables.** `AIState` shared state machine (approach/hunt for bears).

**Verification.** Bear approaches and engages prey credibly.

---

## Cross-phase invariants

- Allocation convention (A/B/C) — `docs/ALLOCATION.md`.
- `rlk_err_t` for all init/generate/process; atomic −1 for Pattern B.
- `RegionSaveState` serializes deltas, never raw buffers.
- rlk renders nothing; `demo/` is opt-in and bridges to pxl.
- Deterministic base never persists; only the mutable overlay does.
