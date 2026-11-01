#ifndef RLK_WG_H
#define RLK_WG_H

/*
 * Deterministic, chunked world generation via noise fields.
 *
 * Everything is a pure function of (seed, wx, wy): chunks are independent.
 * Pipeline: (wx,wy) -> [FIELDS] -> [STAGES] -> [CLASSIFY] -> [WRITE] -> tile
 * (written into a tilemap by rlk_wg_fill)
 *
 * Sampling convention: the fields of tile (wx, wy) are sampled at the tile
 * centre ((float)wx + 0.5f, (float)wy + 0.5f), consistent with
 * rlk_grid_get / rlk_grid_center.
 */

#include <assert.h>
#include <stdint.h>

#include "err.h"
#include "grid.h"
#include "hash.h"
#include "noise.h"
#include "tilemap.h"

/* --- CONSTANTS ----------------------------------------------------------- */
#define RLK_WG_MAX_FIELDS      24       /* max ctx->field slots (sampled+derived) */
#define RLK_WG_MAX_STAGES      16       /* max pipeline stages in wg->stages[] */
#define RLK_WG_MAX_AXES        3        /* max axes; size of zone lo/hi[] */
#define RLK_WG_MAX_ZONES       16       /* max zones in wg->zones[] */
/* Unbounded bound for an axis/zone side: use -/+RLK_WG_AXIS_UNBOUNDED in lo/hi. */
#define RLK_WG_AXIS_UNBOUNDED  1000.0f
/* Stage cfg field/slot index meaning "disabled" (sampled field, derived
   slot or output slot, depending on the cfg field). */
#define RLK_WG_NONE            255

/* --- FORWARD DECL. ------------------------------------------------------- */
typedef struct rlk_wg_s rlk_wg_t;
typedef struct rlk_wg_ctx_s rlk_wg_ctx_t;

/* --- FIELDS -------------------------------------------------------------- */

typedef enum {
    RLK_WG_FBM,     /* fractal Brownian motion */
    RLK_WG_RIDGE    /* ridged multifractal (1-|2v-1|), for mountain ridges */
} rlk_wg_field_kind_t;

typedef struct {
    rlk_wg_field_kind_t kind;
    uint64_t salt;      /* decorrelates this field from the others; mixed with the world seed */
    int octaves;        /* 0 -> 3 */
    float freq;         /* > 0 */
    float gain;         /* 0 -> 0.5 */
} rlk_wg_field_t;

/* --- CLASSIFY: axes & zones ---------------------------------------------- */

/* Axis: projects a field slot into the classification space, the shared
 * metric in which zone boxes are compared (margin, inside, border).
 * Border wobble: jitter_amp > 0 adds (field[jitter_slot]-0.5)*2*jitter_amp to
 * the axis value before comparing, so zone borders wiggle with that slot's
 * noise instead of being straight axis cuts. jitter_amp == 0 disables it. */
typedef struct {
    uint8_t slot;         /* ctx->field slot read as the axis value */
    uint8_t jitter_slot;  /* noise slot displacing the axis value (if amp > 0) */
    float   jitter_amp;   /* max wobble, in axis units (e.g. 0.04) */
} rlk_wg_axis_t;

/* Zone: axis-aligned box over axis values (lo < hi for each used axis). */
typedef struct {
    float lo[RLK_WG_MAX_AXES];
    float hi[RLK_WG_MAX_AXES];

    /* Writer called after the tile is reset and the zone index stored in it.
       Must not be NULL. */
    void (*write)(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out);
    const void *data;
} rlk_wg_zone_t;

/* --- STAGES -------------------------------------------------------------- */

/* Stage callback signature; the contract is on rlk_wg_stage_t below. */
typedef void (*rlk_wg_stage_fn)(rlk_wg_ctx_t *ctx, const void *data);

/* Stage: fn edits ctx (typically ctx->field[]), the world config is ctx->wg.
 * `data` is an opaque stage config that must outlive the wg.
 * Contract (not checked): stages run in registration order, so a stage may
 * only read derived slots written by stages registered before it. */
typedef struct {
    rlk_wg_stage_fn fn;
    const void *data;
} rlk_wg_stage_t;

/* --- CONTEXT & WORLD ----------------------------------------------------- */

/* Per-tile pipeline state, filled per tile by rlk_wg_fill. */
struct rlk_wg_ctx_s {
    const rlk_wg_t *wg;

    int   x, y;                            /* run-local tile index */
    int   wx, wy;                          /* world tile coords */

    uint8_t zone;    /* index into zones[] */
    uint8_t zone2;   /* runner-up zone (== zone if only one) */

    float inside;    /* 0 at the zone border, 1 deep inside (inside_scale) */
    float border;    /* 0 far from zone2, 1 on the zone2 border */

    /* field slots: [0,nsampled) noise-sampled; [nsampled,RLK_WG_MAX_FIELDS)
       stage-written (0.0f until a stage writes them) */
    float field[RLK_WG_MAX_FIELDS];
};

/* World generator: fill the config fields, call rlk_wg_init() once,
   then treat as const. */
struct rlk_wg_s {
    /* --- Config (user fills, then call rlk_wg_init) --- */
    uint64_t seed;

    /* FIELDS */
    rlk_wg_field_t fields[RLK_WG_MAX_FIELDS];
    int nsampled;                /* 1..RLK_WG_MAX_FIELDS; [0,nsampled) noise-sampled, the rest stage-written */

    /* STAGES (run in order). nstages is managed by rlk_wg_add(). */
    rlk_wg_stage_t stages[RLK_WG_MAX_STAGES];
    int nstages;

    /* CLASSIFY */
    rlk_wg_axis_t axes[RLK_WG_MAX_AXES];
    int naxes;                   /* 1..RLK_WG_MAX_AXES; >= 1 required */

    rlk_wg_zone_t zones[RLK_WG_MAX_ZONES];
    int nzones;                  /* 1..RLK_WG_MAX_ZONES */

    float inside_scale;          /* > 0: margin that counts as deep inside */
    float border_width;          /* > 0: width of the zone2 border band */

    /* --- Runtime (filled by rlk_wg_init, do not touch) --- */
    rlk_noise_t noise[RLK_WG_MAX_FIELDS];  /* one seeded generator per field */
};

/* --- PUBLIC API ---------------------------------------------------------- */

/* Register a pipeline stage. Stages run in registration order: a stage may
 * only read derived slots written by stages registered before it (contract,
 * not checked). Returns RLK_E_INVALID if the stage table is full.
 * Call before the first rlk_wg_fill().
 * Ready-made stages: rlk_wg_<module>(&cfg) builds a stage from a config,
 * resolving its zero defaults in place (treat the cfg as const afterwards).
 * A custom stage is registered as
 * rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = my_fn, .data = &cfg }). */
rlk_err_t rlk_wg_add(rlk_wg_t *wg, rlk_wg_stage_t stage);

/* Initialize: resolves zero defaults (octaves->3, gain->0.5), asserts the
   config (safety checks), and seeds the noise generators. Call once after
   filling the config fields (fields, axes, zones); stages may be registered
   before or after init, but before the first rlk_wg_fill. Stage order is the
   registration order (see rlk_wg_add). */
void rlk_wg_init(rlk_wg_t *wg);

/* Fill tm over its own world rect (tm->w x tm->h tiles at the tilemap's
   origin; origin_x/origin_y must equal it, asserted). Tiles are
   evaluated row-major, fields sampled at the tile centre (wx+0.5, wy+0.5).
   Chunked generation: one tilemap per chunk, its origin set to
   (cx*tm->w, cy*tm->h), then rlk_wg_fill(wg, cx*tm->w, cy*tm->h, tm). */
void rlk_wg_fill(const rlk_wg_t *wg, int origin_x, int origin_y, rlk_tilemap_t *tm);

/* Fill grid g with wg field idx: each cell gets the field sampled at its
   centre (rlk_grid_center), the same convention as the per-tile field pass.
   Macro driver: project a field onto a coarse caller-owned grid (e.g. the
   elevation grid a drain runs on). Call after rlk_wg_init; g->values must
   hold g->lattice.w * g->lattice.h floats. */
void rlk_wg_fill_grid(const rlk_wg_t *wg, int idx, rlk_grid_t *g);

/* --- HELPERS ------------------------------------------------------------- */

/* Re-sample a wg field at an arbitrary float world position.
 * Same FBM/RIDGE sampling as the field pass (same noise generator).
 * For stages that need to sample a field at a non-tile position (e.g. creeks,
 * domain-warped relief). To get the field value of tile (wx, wy), pass the
 * tile centre (wx + 0.5f, wy + 0.5f). */
float rlk_wg_sample_field(const rlk_wg_t *wg, int idx, float x, float y);

/* Deterministic per-tile random in [0,1). Salt decorrelates decisions. */
static inline float
rlk_wg_rand(const rlk_wg_t *wg, int wx, int wy, uint64_t salt) {
    assert(wg);
    return rlk_hash_normf(rlk_hash_at(wg->seed, wx, wy, salt));
}

#endif /* RLK_WG_H */
