/*
 * gen_stages - wg pipeline stages of the tilemap_gen demo (see gen_internal.h).
 *
 * province / rain-shadow / strahler wrappers, light geology (geo_eval +
 * geology_stage), the coarse-grid elevation function (gen_relief_elev_fn,
 * shared with the drain grid so rivers follow the displayed terrain) and the
 * terrain-constrained POI detector stage.
 */

#include <math.h>

#include "gen_internal.h"

/* Apply the province region bias to a single field value at float position
   (x, y), through the stock region stage on a throwaway ctx: only the Voronoi
   weights depend on (x, y), the other field values are irrelevant (their
   bias/gain are 0). Used to bias the coarse drain grid exactly like the tile
   pipeline. */
static float
gen_province_mod_field(const tilemap_gen_t *gen, int f, float value, float x, float y) {
    rlk_wg_ctx_t ctx = {0};
    ctx.wg = &gen->wg;
    ctx.wx = (int)floorf(x);
    ctx.wy = (int)floorf(y);
    for (int i = 0; i < gen->wg.nsampled; i++)
        ctx.field[i] = 0.5f;
    ctx.field[f] = value;
    gen->prov_mod.fn(&ctx, gen->prov_mod.data);
    return ctx.field[f];
}

/* Stage 0: provinces (specialist review 2026-10-30, geographer P0: 4-5 Voronoi
   provinces with per-type field bias, applied before relief/rivers).
   Re-sample F_HUM at a large coordinate offset (at low
   climate freqs the 128x96 map only covers a few lattice cells, so fields
   sampled at the same origin can correlate spuriously; the offset
   decorrelates humidity for any seed), then run the stock region stage:
   per-province bias/gain on the sampled fields, before relief/rivers. */
void
gen_province_stage(rlk_wg_ctx_t *ctx, const void *data) {
    const tilemap_gen_t *gen = (const tilemap_gen_t *)data;
    ctx->field[F_HUM] = rlk_wg_sample_field(ctx->wg, F_HUM,
                                            (float)ctx->wx + 0.5f + 913.7f,
                                            (float)ctx->wy + 0.5f + 371.3f);
    gen->prov_mod.fn(ctx, gen->prov_mod.data);
}

/* Stage 3 wrapper: stock wg_relief stage, then rain shadow (post-relief;
   specialist review 2026-10-30, geographer P0: orographic rain shadow).
   Dominant wind west -> east: air rising on the west slope of a range
   (elevation increasing eastward) is humid (orographic lift), air sinking
   on the east slope is dry (foehn). The slope is read from the coarse
   elevation grid, i.e. the same terrain the relief stage displays. */
void
gen_relief_rain_stage(rlk_wg_ctx_t *ctx, const void *data) {
    const tilemap_gen_t *gen = (const tilemap_gen_t *)data;
    gen->relief_mod.fn(ctx, gen->relief_mod.data);

    int i, j;
    if (!rlk_grid_cell_of(&gen->elev_grid, ctx->wx, ctx->wy, &i, &j)) return;
    int iw = i > 0 ? i - 1 : i;
    int ie = i < GRID_W - 1 ? i + 1 : i;
    float ew = gen->elev_buf[(size_t)j * GRID_W + (size_t)iw];
    float ee = gen->elev_buf[(size_t)j * GRID_W + (size_t)ie];
    float slope = (ee - ew) / (2.0f * (float)GRID_CELL);   /* per tile, west -> east */
    float bias = slope * 3.0f;                             /* windward +, leeward - */
    if (bias > 0.10f) bias = 0.10f;
    if (bias < -0.10f) bias = -0.10f;
    float hum = ctx->field[F_HUM] + bias;
    ctx->field[F_HUM] = hum < 0.0f ? 0.0f : (hum > 1.0f ? 1.0f : hum);
}

/* Stage: modulate the river mask by Strahler order (specialist review
   2026-10-30, hydrologist P1: width by stream order) — headwater streams
   (order 1-2) run narrow, rivers (3-4) normal, big rivers (5+) wide.
   The mask threshold in the writers (mask > 0.5) turns the scale factor
   into a width change of roughly +/-0.7 tile around the drain SDF width. */
void
gen_strahler_stage(rlk_wg_ctx_t *ctx, const void *data) {
    (void)data;
    float mask = ctx->field[F_RIVER_MASK];
    if (mask <= 0.0f) return;
    float s = ctx->field[F_STRAHLER];
    float f = s <= 2.0f ? 0.7f : (s <= 4.0f ? 1.0f : 1.6f);
    mask *= f;
    ctx->field[F_RIVER_MASK] = mask > 1.0f ? 1.0f : mask;
}

/* --- Light geology -------------------------------------------------------
   Structures are computed from demo-owned noise (geo_noise), shared by the
   coarse grid (gen_relief_elev_fn, so rivers follow the structured terrain)
   and the tile pipeline (gen_geology_stage, post-relief). No plate
   tectonics: faults (horst/graben offset), folds (sinusoidal bands), domes
   (broad uplifts) and a few volcanic cones. */

#define GEO_FAULT_AMP   0.08f   /* vertical offset along a fault line */
#define GEO_FOLD_AMP    0.05f   /* fold undulation amplitude */
#define GEO_DOME_AMP    0.12f   /* dome uplift */
#define GEO_VOLC_AMP    0.16f   /* cone height */
#define GEO_VOLC_RADIUS 7.0f    /* cone radius (tiles) */
#define GEO_VOLC_CELL   36.0f   /* hash cell size for volcano placement */
#define GEO_VOLC_DENS   0.10f   /* probability per cell */

/* Evaluate the geological structures at world position (x, y).
   `elev` is the current elevation estimate (used for the lithology class).
   Returns the elevation bias; st_out/litho_out (either may be NULL) receive
   the dominant structure type and the lithology class. */
float
gen_geo_eval(const tilemap_gen_t *gen, float x, float y, float elev,
             uint8_t *st_out, uint8_t *litho_out) {
    /* Faults: ridged-noise lines; offset sign from a second field */
    float g0 = rlk_noise_fbm(&gen->geo_noise[0], x, y, 3, 0.020f, 0.5f);
    float ridge = 1.0f - fabsf(2.0f * g0 - 1.0f);
    float tf = (ridge - 0.90f) / 0.07f;
    tf = tf < 0.0f ? 0.0f : (tf > 1.0f ? 1.0f : tf);
    float fault = rlk_smoothstep(tf);
    float g1 = rlk_noise_fbm(&gen->geo_noise[1], x, y, 2, 0.015f, 0.5f);
    float sign = g1 > 0.5f ? 1.0f : -1.0f;
    float bias_fault = sign * GEO_FAULT_AMP * fault;

    /* Folds: sinusoidal bands, regional envelope from low-freq noise */
    float g2 = rlk_noise_fbm(&gen->geo_noise[2], x, y, 2, 0.008f, 0.5f);
    float te = (g2 - 0.45f) / 0.20f;
    te = te < 0.0f ? 0.0f : (te > 1.0f ? 1.0f : te);
    float env = rlk_smoothstep(te);
    float ang = 0.9f + 3.0f * (g1 - 0.5f);
    float phase = 6.2831853f * 0.018f * (x * cosf(ang) + y * sinf(ang))
                + 6.2831853f * 2.0f * g2;
    float bias_fold = GEO_FOLD_AMP * sinf(phase) * env;

    /* Domes: broad uplifts where a very-low-freq field is high */
    float g3 = rlk_noise_fbm(&gen->geo_noise[2], x, y, 2, 0.011f, 0.5f);
    float td = (g3 - 0.66f) / 0.14f;
    td = td < 0.0f ? 0.0f : (td > 1.0f ? 1.0f : td);
    float dome = rlk_smoothstep(td);
    float bias_dome = GEO_DOME_AMP * dome;

    /* Volcanoes: rare point cones on a coarse hash grid (3x3 cell search) */
    float bias_volc = 0.0f;
    float vdist = 1.0e9f;
    int cx = (int)floorf(x / GEO_VOLC_CELL);
    int cy = (int)floorf(y / GEO_VOLC_CELL);
    for (int j = cy - 1; j <= cy + 1; j++) {
        for (int i = cx - 1; i <= cx + 1; i++) {
            if (rlk_wg_rand(&gen->wg, i, j, 0xB01CULL) > GEO_VOLC_DENS) continue;
            float vx = ((float)i + 0.5f + (rlk_wg_rand(&gen->wg, i, j, 0xA11CEULL) - 0.5f) * 0.6f) * GEO_VOLC_CELL;
            float vy = ((float)j + 0.5f + (rlk_wg_rand(&gen->wg, i, j, 0xD0D0ULL) - 0.5f) * 0.6f) * GEO_VOLC_CELL;
            float d = rlk_dist_euclideanf(x, y, vx, vy);
            if (d < vdist) vdist = d;
            if (d < GEO_VOLC_RADIUS) {
                float cone = GEO_VOLC_AMP * (1.0f - d / GEO_VOLC_RADIUS);
                float crater = d < 2.0f ? -0.06f * (1.0f - d / 2.0f) : 0.0f;
                float c = cone + crater;
                if (c > bias_volc) bias_volc = c;
            }
        }
    }

    float bias = bias_fault + bias_fold + bias_dome + bias_volc;

    if (st_out) {
        if (vdist < GEO_VOLC_RADIUS)      *st_out = STRUCT_VOLCANO;
        else if (fault > 0.5f)            *st_out = STRUCT_FAULT;
        else if (dome > 0.5f)             *st_out = STRUCT_DOME;
        else if (env > 0.6f)              *st_out = STRUCT_FOLD;
        else                              *st_out = STRUCT_NONE;
    }
    if (litho_out) {
        if (vdist < GEO_VOLC_RADIUS)      *litho_out = LITHO_EFFUSIVE;
        else if (dome > 0.5f)             *litho_out = LITHO_INTRUSIVE;
        else if (fault > 0.3f || elev > 0.78f) *litho_out = LITHO_META;
        else                              *litho_out = LITHO_SEDIMENT;
    }
    return bias;
}

/* Stage 6: geology. Adds the structural bias to the final elevation (the
   coarse grid got the same bias in gen_relief_elev_fn, so rivers and relief
   agree) and records structure type + lithology per tile. */
void
gen_geology_stage(rlk_wg_ctx_t *ctx, const void *data) {
    tilemap_gen_t *gen = (tilemap_gen_t *)data;
    uint8_t st, litho;
    float bias = gen_geo_eval(gen, (float)ctx->wx, (float)ctx->wy,
                              ctx->field[F_ELEV], &st, &litho);
    ctx->field[F_ELEV] += bias;
    int idx = ctx->y * TM_W + ctx->x;
    gen->struct_buf[idx] = st;
    gen->litho_buf[idx] = litho;
}

/* Elevation grid function: the wg_relief cfg's BASE elevation (cont + hills +
   mountains, before river/fjord carving) evaluated at an arbitrary float
   position, so the drain grid sees the same terrain the tile pipeline
   displays — rivers follow the displayed relief. All relief parameters come
   from the stage cfg (single source of truth); the province bias goes
   through the stock region stage. Sampled at the true cell centre
   (rlk_grid_center convention), like rlk_wg_fill_grid. Needs rlk_wg_init
   first (fields sampled via rlk_wg_sample_field). */
float
gen_relief_elev_fn(float px, float py, const void *user) {
    const gen_relief_ctx_t *ctx = (const gen_relief_ctx_t *)user;
    const rlk_wg_t *wg = ctx->wg;
    const rlk_wg_relief_cfg_t *rc = ctx->relief_cfg;

    /* Continent at original position, province-biased through the stock
       region stage: the drain grid and the displayed relief then share the
       same coastlines. */
    float cont = rlk_wg_sample_field(wg, rc->f_cont, px, py);
    cont = gen_province_mod_field(ctx->gen, rc->f_cont, cont, px, py);

    /* Domain warp */
    float vx = rlk_wg_sample_field(wg, rc->f_warp_x, px, py);
    float vy = rlk_wg_sample_field(wg, rc->f_warp_y, px, py);
    float qx = px + (vx - 0.5f) * 2.0f * rc->warp;
    float qy = py + (vy - 0.5f) * 2.0f * rc->warp;

    /* Base elevation: cont + hills + mountains */
    float elev = cont;
    float hills = rlk_wg_sample_field(wg, rc->f_hills, qx, qy);
    elev += rc->hills_amp * (hills - 0.5f);
    float ridge = rlk_wg_sample_field(wg, rc->f_ridge, qx, qy);
    float mt = rlk_smoothstep(rlk_clampf((cont - rc->mountain_lo) / (rc->mountain_hi - rc->mountain_lo),
                                         0.0f, 1.0f));
    elev += rc->mountain_amp * mt * ridge;

    /* Light geology: same structural bias as the tile pipeline (gen_geology_stage) */
    elev += gen_geo_eval(ctx->gen, px, py, elev, NULL, NULL);

    /* The drain grid requires elevation in [0, RLK_DRAIN_SCALE): clamp the
       summed amplitudes (hills + mountains + geology can overflow 1.0). */
    return rlk_clampf(elev, 0.0f, 1.0f - 1.0f / RLK_DRAIN_SCALE);
}

/* Stage: latitudinal thermal gradient (specialist review 2026-10-30,
   geographer): F_TEMP only knows altitudinal lapse cooling, no latitude —
   yet the map is ~64 tiles "tall". Subtract a linear gradient peaking at
   the map top/bottom edges (|py| / lat_half_span). Runs after the relief
   stage, which write-backs the lapse-cooled F_TEMP. The Köppen classify
   axis reads the result (D_TEMP_LAT) instead of raw F_TEMP; POI detectors
   keep raw F_TEMP. */
void
gen_temp_lat_stage(rlk_wg_ctx_t *ctx, const void *data) {
    const gen_temp_lat_cfg_t *c = (const gen_temp_lat_cfg_t *)data;
    const rlk_wg_t *wg = ctx->wg;
    const float py = (float)ctx->wy + 0.5f;
    const float lat = fabsf(py) / c->lat_half_span;
    float t = ctx->field[c->f_temp] - c->temp_drop * lat;
    ctx->field[wg->nsampled + c->slot_out] = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

/* --- POI: terrain-constrained detectors -----------------------------------
   The stock rlk_wg_poi stages cover one tested field (threshold) plus
   optional drain/grid constraints. This stage adds kinds constrained by
   multi-field ranges (elev/temp/hum/river/slope) and geology bitmasks
   (litho/struct buffers, not wg fields), all readable from the per-tile ctx
   and the geology buffers (geology stage runs before, per tile). Boundary:
   multi-field ranges stay here until a second real consumer appears (YAGNI);
   then generalize rlk_wg_poi_cfg_t to a small table of (field, lo, hi). */

typedef struct {
    uint8_t type;
    uint64_t salt;
    float elev_min, elev_max;       /* on F_ELEV (post-geology, same value classify sees) */
    float temp_min, temp_max;       /* on F_TEMP (-1 = ignore) */
    float hum_min, hum_max;         /* on F_HUM (-1 = ignore) */
    float river_min;                /* min F_RIVER_MASK (-1 = ignore) */
    float slope_min, slope_max;     /* on F_SLOPE (-1 = ignore) */
    uint16_t litho_mask;            /* allowed LITHO_* bits (0 = any) */
    uint16_t struct_mask;           /* allowed STRUCT_* bits (0 = any) */
    float density;                  /* probability per eligible tile */
    uint16_t size;                  /* bbox side in tiles */
    int min_sep;                    /* min tiles between POIs of same type */
} poi_det_t;

#define ANY (-1.0f)
#define RANGE (1.0e9f)
#define BIT(v) (1u << (v))

/* Zone-equivalent constraints expressed on the axis fields (stages run
   before classify, so ctx->zone is not available yet): beach = elev
   [ELEV_SHALLOW, ELEV_BEACH], lowland biomes = [ELEV_BEACH, ELEV_LOWLAND],
   mountain/snow = [ELEV_LOWLAND, 1], temperate band = temp [TEMP_COLD, TEMP_HOT]. */
static const poi_det_t POI_DETS[] = {
    /* hamlet: small settlement on temperate lowlands (plains or steppe) */
    { POI_HAMLET, 0xBE01ULL,
      ELEV_BEACH + 0.02f, ELEV_LOWLAND - 0.04f, TEMP_COLD, TEMP_HOT, ANY, RANGE,
      -1.0f, -1.0f, RANGE, 0, 0,
      0.004f, 8, 14 },
    /* mine: mountain faults/domes, metamorphic or igneous rock */
    { POI_MINE, 0xBE02ULL,
      ELEV_LOWLAND, 1.0f, ANY, RANGE, ANY, RANGE, -1.0f, -1.0f, RANGE,
      (uint16_t)(BIT(LITHO_META) | BIT(LITHO_INTRUSIVE) | BIT(LITHO_EFFUSIVE)),
      (uint16_t)(BIT(STRUCT_FAULT) | BIT(STRUCT_DOME)),
      0.004f, 8, 18 },
    /* mill: lowland river reach with enough slope to turn a wheel */
    { POI_MILL, 0xBE03ULL,
      ELEV_BEACH + 0.01f, ELEV_LOWLAND - 0.02f, ANY, RANGE, ANY, RANGE,
      0.5f, 0.004f, 0.05f, 0, 0,
      0.004f, 6, 18 },
    /* port: beach at a river mouth (estuary) */
    { POI_PORT, 0xBE04ULL,
      ELEV_SHALLOW, ELEV_BEACH, ANY, RANGE, ANY, RANGE,
      0.2f, -1.0f, RANGE, 0, 0,
      0.02f, 8, 24 },
    /* lighthouse: cliff coast (steep beach) */
    { POI_LIGHTHOUSE, 0xBE05ULL,
      ELEV_SHALLOW, ELEV_BEACH, ANY, RANGE, ANY, RANGE,
      -1.0f, SLOPE_STEEP, RANGE, 0, 0,
      0.05f, 4, 24 },
    /* fort: hilltop at the mountain foot */
    { POI_FORT, 0xBE06ULL,
      ELEV_LOWLAND - 0.06f, ELEV_LOWLAND - 0.01f, ANY, RANGE, ANY, RANGE,
      -1.0f, -1.0f, RANGE, 0, 0,
      0.002f, 10, 28 },
};
#define POI_DET_COUNT (sizeof(POI_DETS) / sizeof(POI_DETS[0]))

/* Terrain-constrained POI detectors (table-driven). */
void
gen_poi_terrain_stage(rlk_wg_ctx_t *ctx, const void *data) {
    tilemap_gen_t *gen = (tilemap_gen_t *)data;
    if (ctx->wx < 0 || ctx->wx >= TM_W || ctx->wy < 0 || ctx->wy >= TM_H)
        return;
    const int idx = ctx->y * TM_W + ctx->x;
    for (size_t k = 0; k < POI_DET_COUNT; k++) {
        const poi_det_t *d = &POI_DETS[k];
        float elev = ctx->field[F_ELEV];
        if (elev < d->elev_min || elev > d->elev_max)
            continue;
        if (d->temp_min >= 0.0f) {
            float t = ctx->field[F_TEMP];
            if (t < d->temp_min || t > d->temp_max)
                continue;
        }
        if (d->hum_min >= 0.0f) {
            float h = ctx->field[F_HUM];
            if (h < d->hum_min || h > d->hum_max)
                continue;
        }
        if (d->river_min >= 0.0f && ctx->field[F_RIVER_MASK] < d->river_min)
            continue;
        if (d->slope_min >= 0.0f) {
            float s = ctx->field[F_SLOPE];
            if (s < d->slope_min || s > d->slope_max)
                continue;
        }
        if (d->struct_mask && !(d->struct_mask & (1u << gen->struct_buf[idx])))
            continue;
        if (d->litho_mask && !(d->litho_mask & (1u << gen->litho_buf[idx])))
            continue;
        if (rlk_wg_rand(ctx->wg, ctx->wx, ctx->wy, d->salt) >= d->density)
            continue;
        if (rlk_poi_find(&gen->pois, ctx->wx, ctx->wy, d->min_sep, d->type))
            continue;
        int hs = (int)d->size / 2;
        rlk_poi_t poi = {
            .x = ctx->wx, .y = ctx->wy,
            .bbox = { ctx->wx - hs, ctx->wy - hs, (int)d->size, (int)d->size },
            .type = d->type,
            .seed = rlk_poi_seed(ctx->wg->seed, ctx->wx, ctx->wy, d->type, d->salt),
            .gen = 0, .level = 0,
        };
        (void)rlk_poi_push(&gen->pois, &poi);
    }
}
