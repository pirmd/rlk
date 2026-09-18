# Allocation convention

rlk exposes every memory need up front. **The library never calls
`malloc`/`calloc`/`realloc`/`free` internally.** The caller owns all backing
memory and chooses its source (heap, pool, stack, static). This is the same
posture already held by [pxl](https://github.com/pirmd/pxl), whose core
contains zero allocations (all `malloc`/`free` live in `demo/`, `tool/`,
and tests, and `pxl_buf_t` is a caller-provided pointer).

One convention, three patterns, applied uniformly across every module.

---

## Error type

All `init`/`generate`/`process` entry points return `rlk_err_t` (error
codes), **not `bool`**. Multiple failure causes exist (buffer too small,
invalid config, …) and `bool` erases them. Asserts are permitted internally
for invariants but must not be the only guard against caller misuse — they
vanish in release builds.

```c
typedef enum {
    RLK_OK            = 0,
    RLK_E_BUFSIZE     = -1,   /* provided buffer smaller than _required_size */
    RLK_E_CONFIG      = -2,   /* invalid configuration */
    RLK_E_CAPACITY     = -3,  /* output capacity insufficient (Pattern B) */
    RLK_E_SCRATCH      = -4,   /* scratch buffer too small (Pattern C) */
    /* ... module-specific negative codes ... */
} rlk_err_t;
```

---

## Pattern A — fixed size known from config

For structures whose size is fully determined by configuration at call time
(`Tilemap`, `WorldMap`, `EntityStore`).

```c
size_t X_required_size(const XConfig *cfg);          /* exact byte size */
rlk_err_t X_init(X *out, const XConfig *cfg,
                 void *buffer, size_t buffer_size);   /* fails RLK_E_BUFSIZE */
```

- `X_init` assigns fields and zero-inits the buffer; it does **not** allocate.
- `X_init` must check `buffer_size >= X_required_size(cfg)` and return
  `RLK_E_BUFSIZE` cleanly on mismatch — never corrupt silently.

```c
size_t sz = tilemap_required_size(&(TilemapCfg){.width=256,.height=256});
void *buf = malloc(sz);
Tilemap tm;
if (tilemap_init(&tm, &cfg, buf, sz) != RLK_OK) { /* ... */ }
```

## Pattern B — variable-size result (caller provides capacity)

For functions whose output count is not known in advance (`poi_place`,
`resource_layer_scatter`). The caller dimensions `out[]` via an estimate.

```c
int X_estimate_count(...);                           /* upper bound, not exact */
int X_compute(..., YOut *out, int out_capacity);      /* returns count or -1 */
```

- Returns the number of elements actually written on success.
- Returns **−1** when `out_capacity` is insufficient.

**Failure convention — atomic (rule, applied everywhere):**
> On −1, **nothing has been written** to `out`. The caller retries with
> `estimate_count` and a larger buffer. There is no "partial result" mode.

This must hold for every Pattern B function. `resource_layer_scatter` is
noted as currently "truncates silently" in the design notes — it is to be
**harmonized to atomic −1** before it is exposed, so the convention is
uniform and the user need not memorize per-module behavior.

## Pattern C — internal scratch (separate lifetime)

For passes needing transient workspace (flood-fill, flow-accumulation sort,
Poisson-disk candidate queues). Scratch lifetime = the call; the result
buffer lifetime = the session/region. Two buffers, two lifetimes, visible in
the signature.

```c
size_t X_scratch_size(const XConfig *cfg);
rlk_err_t X_process(X *target, void *scratch, size_t scratch_size);
```

- The caller may free scratch **immediately** after the call returns.
- The caller may reuse a **single scratch buffer across all passes** by
  sizing it as `max(passA_scratch_size, passB_scratch_size, …)`. This is
  safe because no two passes consume scratch simultaneously.
- Documented limitation: **do not pass the same scratch to a nested call**
  that itself uses scratch — it would alias and corrupt the outer pass.
  Sequential reuse is fine; nested reuse is not.

---

## `EntityStore` — Pattern A with internal carving

A single caller-provided buffer is carved internally into per-component
arrays (`pos_x[]`, `pos_y[]`, `radius[]`, `layer[]`, `alive[]`, …):

```c
size_t entity_store_required_size(int32_t max_entities);
rlk_err_t entity_store_init(EntityStore *s, int32_t max_entities,
                             void *buffer, size_t buffer_size);
EntityId entity_store_create(EntityStore *s);   /* recycles dead slot, -1 if full */
void     entity_store_destroy(EntityStore *s, EntityId id);
```

- Capacity is fixed once at startup; **no dynamic growth in-game** — coherent
  with the refusal of hidden allocation during the game tick.
- `entity_store_required_size` is the **sole source of truth** for the
  internal layout. Adding a component changes it; therefore save formats
  must serialize **only live slots**, never the raw buffer, so saves stay
  insulated from layout/version changes (see ARCHITECTURE.md → RegionSaveState).

---

## Exception — static dispatch table (documented)

The interaction dispatch table `handler[LAYER_A][LAYER_B]` is **static
memory** with compile-time-fixed `LAYER_COUNT`. It is **not** a runtime
allocation, so it is exempt from the `_required_size` convention.

- This is the one justified exception: size depends on nothing the caller
  controls at runtime.
- It re-enters the convention **only if** `LAYER_COUNT` becomes dynamic
  (user-registered layers at runtime). That is not planned for V1; if it
  happens, Pattern A applies (`dispatch_required_size(layer_count)`).
- Multiple interaction tables (multi-world) would also motivate making it
  caller-provided; not a V1 need.

---

## Verification rule

Every module ships a test that:
1. Calls `_required_size`/`_scratch_size`/`estimate_count`,
2. Provides a deliberately too-small buffer and asserts the documented
   error (`RLK_E_BUFSIZE` / `RLK_E_SCRATCH` / −1),
3. Provides the exact size and asserts success,
4. (Pattern C) frees scratch immediately after and confirms the result is
   still valid.
