#ifndef RLK_WG_H
#define RLK_WG_H

/*
 * Deterministic, chunked world generation via noise fields.
 *
 * Everything is a pure function of (seed, wx, wy): chunks are independent.
 * Pipeline: (wx,wy) -> [FIELDS] -> [STAGES] -> [CLASSIFY] -> [WRITE] -> tile
 *
 * Sampling convention: the fields of tile (wx, wy) are sampled at the tile
 * centre ((float)wx + 0.5f, (float)wy + 0.5f), consistent with
 * rlk_grid_sample / rlk_grid_center.
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include "err.h"
#include "grid.h"
#include "hash.h"
#include "noise.h"
#include "tilemap.h"

/* --- CONSTANTS ----------------------------------------------------------- */
#define RLK_WG_MAX_FIELDS      20
#define RLK_WG_MAX_ZONES       16
#define RLK_WG_MAX_STAGES      12
#define RLK_WG_MAX_AXES        3
#define RLK_WG_OPEN        1000.0f  /* Unbound axis range (-OPEN, +OPEN) */

/* --- TYPES --------------------------------------------------------------- */

/* Field kinds for noise generation:
 *   RLK_WG_FBM   - fractal Brownian motion (sum of octaves)
 *   RLK_WG_RIDGE - ridged multifractal (1 - |2v-1|), good for mountain ridges */
typedef enum { RLK_WG_FBM, RLK_WG_RIDGE } rlk_wg_field_kind_t;

/* Noise field configuration */
typedef struct {
    rlk_wg_field_kind_t kind;
    uint64_t seed;
    int octaves;    /* 0 -> 3 */
    float freq;     /* > 0 */
    float gain;     /* 0 -> 0.5 */
} rlk_wg_field_t;

/* Axis: projects a field slot into the classification space. The axes define
 * a shared metric space in which zones are compared (margin, depth, edge).
 * jitter_amp == 0 disables border wobble. */
typedef struct {
    uint8_t field;         /* slot read as the axis value */
    uint8_t jitter_field;  /* slot used for wobble (ignored if amp == 0) */
    float   jitter_amp;    /* max wobble, in axis units (e.g. 0.04) */
} rlk_wg_axis_t;

/* --- Zone ---------- */

typedef struct rlk_wg_ctx_s rlk_wg_ctx_t;

/* Zone: axis-aligned box over axis values (lo < hi for each used axis). */
typedef struct {
    float lo[RLK_WG_MAX_AXES];
    float hi[RLK_WG_MAX_AXES];

    /* Writer called after biome is set to the zone index. Must not be NULL. */
    void (*write)(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out);
    const void *data;
} rlk_wg_zone_t;

/* --- Pipeline & ctx & stage ---------- */

/* Pipeline stage: runs between field sampling and classify.
 * Acts on ctx (typically modifies field[]).
 * `data` is an opaque user pointer (stage config), must outlive the wg.
 * Access the world config via ctx->wg.
 */
typedef void (*rlk_wg_stage_fn)(rlk_wg_ctx_t *ctx, const void *data);

/* Slot set declared by a stage, in the two cfg index spaces:
 *   .sampled bit i = sampled field i (ctx->field[i], i < nsampled)
 *   .derived bit d = derived slot d  (ctx->field[nsampled + d], d < nderived)
 * RLK_WG_SLOTS_ALL selects every slot of the space (e.g. the modulator,
 * which reads and writes all sampled fields in place). */
#define RLK_WG_SLOTS_ALL  0xFFFFFFFFu
#define RLK_WG_SLOT(i)    (1u << (i))

typedef struct {
    uint32_t sampled;
    uint32_t derived;
} rlk_wg_slots_t;

/* Stage: edits ctx->field[]. `data` must outlive the wg.
 * reads/writes declare the slots the stage touches; rlk_wg_init() validates
 * them (see below). Custom stages fill them with RLK_WG_SLOT()/RLK_WG_SLOTS_ALL. */
typedef struct {
    rlk_wg_stage_fn fn;
    const void *data;
    rlk_wg_slots_t reads;   /* slots read by the stage */
    rlk_wg_slots_t writes;  /* slots written by the stage */
} rlk_wg_stage_t;

typedef struct rlk_wg_s rlk_wg_t;

/* Per-tile pipeline state: filled by eval, mutated by stages, read by the zone writer. */
struct rlk_wg_ctx_s {
    const rlk_wg_t *wg;

    int   x, y;                            /* chunk-local (set by fill only) */
    int   wx, wy;                          /* world tile coords */
    float field[RLK_WG_MAX_FIELDS];     /* [0, nsampled) noise-sampled; [nsampled, nsampled+nderived) stage-written */

    uint8_t zone;    /* index into zones[] */
    uint8_t zone2;   /* runner-up zone (== zone if only one) */

    float depth;     /* 0 = zone border .. 1 = deep inside (depth into own zone) */
    float edge;      /* 0 = far from zone2 .. 1 = on zone2 border (proximity to edge) */
};

/* World generator: fill the config fields, call rlk_wg_init() once,
   then treat as const. */
struct rlk_wg_s {
    /* --- Config (user fills, then call rlk_wg_init) --- */
    uint64_t seed;

    /* FIELDS */
    rlk_wg_field_t fields[RLK_WG_MAX_FIELDS];
    int nsampled;                /* 1..MAX_FIELDS: fields [0,nsampled) are noise-sampled */
    int nderived;                /* user sets: fields [nsampled, nsampled+nderived) are stage-written */

    /* STAGES (run in order). nstages is managed by rlk_wg_add(). */
    rlk_wg_stage_t stages[RLK_WG_MAX_STAGES];
    int nstages;

    /* CLASSIFY */
    rlk_wg_axis_t axes[RLK_WG_MAX_AXES];
    int naxes;                   /* 1..3, required */

    rlk_wg_zone_t zones[RLK_WG_MAX_ZONES];
    int nzones;                  /* 1..MAX_ZONES */

    float depth_scale;           /* > 0: margin that counts as "deep" */
    float transition_width;      /* > 0: width of the zone2 blend band */

    /* --- Runtime (filled by rlk_wg_init, do not touch) --- */
    rlk_noise_t noise[RLK_WG_MAX_FIELDS];  /* seeded generators, one per sampled field */
};


/* --- HELPERS ------------------------------------------------------------- */

/* Ready-made writer: copies type/flags/variant from a `const rlk_tile_t *`
   given as data (biome stays the zone id). */
static inline void
rlk_wg_write_const(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)ctx;
    const rlk_tile_t *t = (const rlk_tile_t *)data;
    out->type = t->type;
    out->flags = t->flags;
    out->variant = t->variant;
}

/* Deterministic per-tile random in [0,1). Salt decorrelates decisions. */
static inline float
rlk_wg_rand(const rlk_wg_t *wg, int wx, int wy, uint64_t salt) {
    assert(wg);
    uint64_t h = rlk_hash_sub(rlk_hash_combine2(wg->seed, wx, wy), salt);
    return (float)(h >> 40) * (1.0f / 16777216.0f);
}

/* Re-sample a wg field at an arbitrary float world position.
 * Mirrors the field sampling done in eval(): FBM or RIDGE, same noise generator.
 * For stages that need to sample a field at a non-tile position (e.g. creeks,
 * domain-warped relief). To get the field value of tile (wx, wy), pass the
 * tile centre (wx + 0.5f, wy + 0.5f). */
float rlk_wg_sample_field(const rlk_wg_t *wg, int idx, float x, float y);

/* --- PUBLIC API ---------------------------------------------------------- */

/* Register a pipeline stage. Returns RLK_E_INVALID if the stage table is full.
 * The stage's `data` must outlive the wg. Call before rlk_wg_init().
 * Ready-made stages: rlk_wg_<module>(&cfg) builds a stage from a const
 * config. A custom stage is registered as
 * rlk_wg_add(&wg, (rlk_wg_stage_t){ .fn = my_fn, .data = &cfg,
 *                                    .reads = ..., .writes = ... }). */
rlk_err_t rlk_wg_add(rlk_wg_t *wg, rlk_wg_stage_t stage);

/* Initialize: resolves zero defaults (octaves->3, gain->0.5), runs safety
   asserts, validates the stage slot declarations, and seeds the noise
   generators. Call once after filling the config fields and registering
   stages.
   Slot validation: a stage reading a derived slot that no earlier stage
   writes, any referenced slot out of range, or an axis reading a derived
   slot that no stage writes -> RLK_E_INVALID (stage order is explicit).
   Returns RLK_OK on success. */
rlk_err_t rlk_wg_init(rlk_wg_t *wg);

/* Sink: called by rlk_wg_run for each tile of the run, after fields, stages
 * and classify. ctx->x/y are run-local, ctx->wx/wy the world tile. */
typedef void (*rlk_wg_sink_fn)(const rlk_wg_ctx_t *ctx, void *user);

/* Generic driver: evaluate the pipeline over the world rect
 * (origin_x, origin_y, w, h) and call sink per tile, row-major order.
 * Fields are sampled at the tile centre (wx+0.5, wy+0.5). */
void rlk_wg_run(const rlk_wg_t *wg, int origin_x, int origin_y, int w, int h,
                rlk_wg_sink_fn sink, void *user);

/* Fill tm with the world rect (tm->w x tm->h tiles) at world origin
   (origin_x, origin_y). General primitive; origin may be any tile (incl. negative).
   Implemented as a tilemap sink over rlk_wg_run. */
void rlk_wg_fill(const rlk_wg_t *wg, int origin_x, int origin_y, rlk_tilemap_t *tm);

/* Macro pass: fill grid g with the value of slot `slot` (0..nsampled+nderived-1,
 * indexed as in ctx->field), the pipeline evaluated once per cell at the cell
 * centre (rlk_grid_center). With cellsize > 1 the fields are sampled at the
 * true cell centre, not at a tile corner. This is the macro -> micro driver:
 * fill an elevation grid, run drain on it, then instance the micro pass. */
void rlk_wg_fill_slot(const rlk_wg_t *wg, uint8_t slot, rlk_grid_t *g);

/* Generate chunk (cx, cy) into tm. tm must be exactly one chunk; chunk origin
   is (cx*tm->w, cy*tm->h). Convenience wrapper over rlk_wg_fill. */
static inline void
rlk_wg_chunk(const rlk_wg_t *wg, int cx, int cy, rlk_tilemap_t *tm) {
    assert(tm);
    const int64_t origin_x = (int64_t)cx * tm->w;
    const int64_t origin_y = (int64_t)cy * tm->h;
    assert(origin_x >= INT_MIN && origin_x <= INT_MAX && origin_y >= INT_MIN && origin_y <= INT_MAX);
    rlk_wg_fill(wg, (int)origin_x, (int)origin_y, tm);
}

#endif /* RLK_WG_H */
