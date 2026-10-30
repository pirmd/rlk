/*
 * tilemap_gen - fantasy map generator (see tilemap_gen.h).
 * Generation code extracted from the former demo_tilemap_viewer, unchanged.
 */

#include <assert.h>
#include <math.h>

#include "tilemap_gen.h"
#include "hash.h"

/* Elevation thresholds (relief stage output ~[0,1]).
   Beach is a thin slice (0.03, like demo_worldgen) so coasts read naturally. */
#define ELEV_DEEP     0.33f
#define ELEV_SHALLOW  0.36f
#define ELEV_BEACH    0.39f
#define ELEV_LOWLAND  0.70f
#define ELEV_MOUNTAIN 0.85f

/* Climate plane (temp x hum) split for the 6 land biomes.
   F_TEMP is lapse-cooled by the relief stage before classify. */
#define TEMP_COLD 0.35f   /* below: cold band (tundra/taiga) */
#define TEMP_HOT  0.55f   /* above: hot band (desert/forest) */
#define HUM_DRY   0.50f   /* below: dry half of the climate plane */

#define LAKE_MIN_DEPTH 0.02f    /* min filled depth to be a lake tile */
#define RIVER_ACC_MIN  20u      /* drainage area (cells) at which a river starts */
#define RAPID_SLOPE    0.02f    /* D8 slope above which a river tile shows rapids */

/* --- TERRAIN WRITERS ----------------------------------------------------- */

/* Widen the river mask near the coast: up to 2x at the shoreline.
   Uses the final elevation to estimate distance from the sea.
   shore = 0 at waterline, 1 at ELEV_BEACH (landward edge of beach). */
static inline float
estuary_mask(const rlk_wg_ctx_t *ctx, float mask) {
    if (mask <= 0.0f) return 0.0f;
    float elev = ctx->field[F_ELEV];
    float shore = (elev - SEA_LEVEL) / (ELEV_BEACH - SEA_LEVEL);
    shore = shore < 0.0f ? 0.0f : (shore > 1.0f ? 1.0f : shore);
    /* Widen: 1x inland, 2x at the shoreline */
    return mask * (1.0f + (1.0f - shore));
}

/* Apply the province modulator to a single field value at float position
   (x, y), through a throwaway ctx: only the Voronoi weights depend on
   (x, y), the other field values are irrelevant (their bias/gain are 0).
   Used to bias the coarse drain grid exactly like the tile pipeline. */
static float
province_mod_field(const tilemap_gen_t *gen, int f, float value, float x, float y) {
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

/* Stage 0: provinces. Re-sample F_HUM at a large coordinate offset (at low
   climate freqs the 128x96 map only covers a few lattice cells, so fields
   sampled at the same origin can correlate spuriously; the offset
   decorrelates humidity for any seed), then run the stock Voronoi modulator:
   per-province bias/gain on the sampled fields, before relief/rivers. */
static void
province_stage(rlk_wg_ctx_t *ctx, const void *data) {
    const tilemap_gen_t *gen = (const tilemap_gen_t *)data;
    ctx->field[F_HUM] = rlk_wg_sample_field(ctx->wg, F_HUM,
                                               (float)ctx->wx + 0.5f + 913.7f,
                                               (float)ctx->wy + 0.5f + 371.3f);
    gen->prov_mod.fn(ctx, gen->prov_mod.data);
}

/* Stage 3 wrapper: stock wg_relief stage, then rain shadow (post-relief).
   Dominant wind west -> east: air rising on the west slope of a range
   (elevation increasing eastward) is humid (orographic lift), air sinking
   on the east slope is dry (foehn). The slope is read from the coarse
   elevation grid, i.e. the same terrain the relief stage displays. */
static void
relief_rain_stage(rlk_wg_ctx_t *ctx, const void *data) {
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

/* Stage: modulate the river mask by Strahler order — headwater streams
   (order 1-2) run narrow, rivers (3-4) normal, big rivers (5+) wide.
   The mask threshold in the writers (mask > 0.5) turns the scale factor
   into a width change of roughly +/-0.7 tile around the drain SDF width. */
static void
strahler_stage(rlk_wg_ctx_t *ctx, const void *data) {
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
   coarse grid (relief_elev_fn, so rivers follow the structured terrain) and
   the tile pipeline (geology_stage, post-relief). No plate tectonics:
   faults (horst/graben offset), folds (sinusoidal bands), domes (broad
   uplifts) and a few volcanic cones. */

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
static float
geo_eval(const tilemap_gen_t *gen, float x, float y, float elev,
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
   coarse grid got the same bias in relief_elev_fn, so rivers and relief
   agree) and records structure type + lithology per tile. */
static void
geology_stage(rlk_wg_ctx_t *ctx, const void *data) {
    tilemap_gen_t *gen = (tilemap_gen_t *)data;
    uint8_t st, litho;
    float bias = geo_eval(gen, (float)ctx->wx, (float)ctx->wy,
                          ctx->field[F_ELEV], &st, &litho);
    ctx->field[F_ELEV] += bias;
    int idx = ctx->y * TM_W + ctx->x;
    gen->struct_buf[idx] = st;
    gen->litho_buf[idx] = litho;
}

/* Context for relief_elev_fn: reads field configs from wg, mirrors the wg_relief stage. */
typedef struct {
    const tilemap_gen_t *gen;    /* for the province modulator stage */
    const rlk_wg_t *wg;       /* field configs (freq, octaves, gain, kind) */
    const rlk_noise_t *noise;    /* relief_noise[F_NSAMPLED] */
    float warp;
    float hills_amp;
    float mountain_lo, mountain_hi, mountain_amp;
} relief_elev_ctx_t;

/* Sample a wg field at (x,y), reading parameters from wg->fields[idx]. */
static inline float
relief_sample_field(const rlk_wg_t *wg, const rlk_noise_t *noise,
                    int idx, float x, float y) {
    const rlk_wg_field_t *f = &wg->fields[idx];
    float v = rlk_noise_fbm(&noise[idx], x, y, f->octaves, f->freq, f->gain);
    if (f->kind == RLK_WG_RIDGE)
        v = 1.0f - fabsf(2.0f * v - 1.0f);
    return v;
}

/* Elevation grid function: reproduces the wg_relief stage's BASE elevation
   (cont + hills + mountains, before river/fjord carving), so the drain grid
   sees the same terrain that is displayed. Rivers follow the displayed relief.
   Field parameters are read from wg->fields[] to stay in sync with the stage. */
static float
relief_elev_fn(float px, float py, const void *user) {
    const relief_elev_ctx_t *ctx = (const relief_elev_ctx_t *)user;
    const rlk_wg_t *wg = ctx->wg;

    /* Continent at original position, with the province bias applied through
       the same stock modulator stage as the tile pipeline: the drain grid and
       the displayed relief then share the same coastlines. */
    float cont = relief_sample_field(wg, ctx->noise, F_CONT, px, py);
    cont = province_mod_field(ctx->gen, F_CONT, cont, px, py);

    /* Domain warp */
    float vx = relief_sample_field(wg, ctx->noise, F_WARP_X, px, py);
    float vy = relief_sample_field(wg, ctx->noise, F_WARP_Y, px, py);
    float qx = px + (vx - 0.5f) * 2.0f * ctx->warp;
    float qy = py + (vy - 0.5f) * 2.0f * ctx->warp;

    /* Base elevation: cont + hills + mountains */
    float elev = cont;

    float hills = relief_sample_field(wg, ctx->noise, F_HILLS, qx, qy);
    elev += ctx->hills_amp * (hills - 0.5f);

    /* Mountains: RIDGE kind, only where cont is high enough (smoothstep gate) */
    float ridge = relief_sample_field(wg, ctx->noise, F_RIDGE, qx, qy);
    float mt = (cont - ctx->mountain_lo) / (ctx->mountain_hi - ctx->mountain_lo);
    mt = mt < 0.0f ? 0.0f : (mt > 1.0f ? 1.0f : mt);
    mt = mt * mt * (3.0f - 2.0f * mt);
    elev += ctx->mountain_amp * mt * ridge;

    /* Light geology: same structural bias as the tile pipeline (geology_stage) */
    elev += geo_eval(ctx->gen, px, py, elev, NULL, NULL);

    return elev;
}

/* Capture writer: stores derived fields then dispatches to the real writer. */
static void
capture_write(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    const tilemap_gen_capture_t *cap = (const tilemap_gen_capture_t *)data;
    const int idx = ctx->y * TM_W + ctx->x;
    cap->gen->final_elev[idx] = ctx->field[F_ELEV];
    cap->gen->river_mask_buf[idx] = ctx->field[F_RIVER_MASK];
    cap->gen->river_carve_buf[idx] = ctx->field[F_RIVER_CARVE];
    cap->write(ctx, cap->real_data, out);
}

/* Rock shade by lithology: basalt darkest, granite lightest, metamorphic
   mid-tone, sedimentary keeps the depth-based ramp. */
static uint8_t
rock_variant(const rlk_wg_ctx_t *ctx, const tilemap_gen_t *gen, float depth) {
    int idx = ctx->y * TM_W + ctx->x;
    switch (gen->litho_buf[idx]) {
    case LITHO_EFFUSIVE:  return 3;
    case LITHO_INTRUSIVE: return (uint8_t)(depth * 2.0f);
    case LITHO_META: {
        float v = 1.0f + depth * 2.0f;
        return v > 3.0f ? 3 : (uint8_t)v;
    }
    default:              return (uint8_t)(depth * 3.0f);
    }
}

/* Côtes variées: largeur de plage = f(pente locale, F_SLOPE D8).
   Pente douce -> le sable remonte loin dans les terres (plage large),
   pente raide -> falaise (rocher). Ne s'applique que dans la bande
   côtière [ELEV_BEACH, ELEV_BEACH + COAST_EXT] des zones terrestres. */
#define COAST_EXT     0.06f   /* épaisseur max de la remontée de sable */
#define SLOPE_GENTLE  0.01f   /* en dessous: plage large */
#define SLOPE_STEEP   0.03f   /* au-dessus: falaise */

static int
write_coast(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    float elev = ctx->field[F_ELEV];
    if (elev < ELEV_BEACH || elev > ELEV_BEACH + COAST_EXT) return 0;
    float slope = ctx->field[F_SLOPE];
    if (slope > SLOPE_STEEP) {
        out->type = TILE_ROCK;
        out->flags = 0x01;
        out->variant = rock_variant(ctx, (const tilemap_gen_t *)data, ctx->depth);
        return 1;
    }
    if (slope < SLOPE_GENTLE) {
        out->type = TILE_SAND;
        out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
        return 1;
    }
    return 0;
}

static void
write_deep(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->type = TILE_WATER;
    /* Sediment plume: lighter water where a river meets the sea */
    if (ctx->field[F_RIVER_MASK] > 0.0f) {
        out->variant = 1;
    } else {
        out->variant = (uint8_t)(ctx->depth * 3.0f);
    }
}

static void
write_shallow(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->type = TILE_WATER;
    /* Sediment plume near river mouths: lightest water shade */
    if (ctx->field[F_RIVER_MASK] > 0.0f) {
        out->variant = 0;
    } else {
        out->variant = 1;  /* shallow: medium-light blue */
    }
}

static void
write_beach(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    float mask = estuary_mask(ctx, ctx->field[F_RIVER_MASK]);

    if (mask > 0.0f) {
        /* Estuary: gradient from water (at shoreline) to marsh (inland).
           ctx->depth = 0 at waterline, 1 at landward edge of beach. */
        float eff = mask * (1.0f - 0.5f * ctx->depth);
        if (eff > 0.45f) {
            out->type = TILE_WATER;
            out->variant = 0;
            out->flags = 0x02;
        } else {
            out->type = TILE_MARSH;
            out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
            out->flags = 0x02;
        }
        return;
    }
    /* Cliff: steep shore -> rock instead of sand */
    if (ctx->field[F_SLOPE] > SLOPE_STEEP) {
        out->type = TILE_ROCK;
        out->flags = 0x01;
        out->variant = rock_variant(ctx, (const tilemap_gen_t *)data, ctx->depth);
        return;
    }
    out->type = TILE_SAND;
    /* Wet sand near the waterline (depth=0, darker), dry sand inland (depth=1, lighter) */
    out->variant = (uint8_t)((1.0f - ctx->depth) * 3.0f);
}

/* Shared land prelude: lake, river channel, marsh floodplain.
   Returns 1 if out was written (caller must not touch it further). */
static int
write_land_water(const rlk_wg_ctx_t *ctx, rlk_tile_t *out) {
    /* Lake: filled depression. At the shore with a river, show a channel. */
    if (ctx->field[F_LAKE] >= LAKE_MIN_DEPTH) {
        float mask = estuary_mask(ctx, ctx->field[F_RIVER_MASK]);
        if (mask > 0.3f && ctx->field[F_LAKE] < LAKE_MIN_DEPTH * 2.0f) {
            out->type = TILE_RIVER;
            out->variant = (uint8_t)(mask * 3.0f);
        } else {
            out->type = TILE_LAKE;
            out->variant = 0;
        }
        out->flags = 0x02;
        return 1;
    }
    /* River (widened near coast by estuary_mask) */
    float mask = estuary_mask(ctx, ctx->field[F_RIVER_MASK]);
    if (mask > 0.5f) {
        out->type = TILE_RIVER;
        /* Rapids on steep reaches: lightest shade (foam) */
        out->variant = ctx->field[F_SLOPE] > RAPID_SLOPE ? 0
                                                       : (uint8_t)(mask * 3.0f);
        out->flags = 0x02;
        return 1;
    }
    /* Marsh: floodplain near the coast (river edge or valley, low elevation) */
    float carve = ctx->field[F_RIVER_CARVE];
    if ((mask > 0.0f || carve > 0.0f) && ctx->field[F_ELEV] < ELEV_BEACH + 0.08f) {
        out->type = TILE_MARSH;
        out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
        out->flags = 0x02;
        return 1;
    }
    return 0;
}

/* Rocky outcrops near mountain borders (shared by lowland biomes). */
static void
land_rock_outcrop(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (ctx->zone2 == Z_MOUNTAIN && ctx->edge > 0.6f &&
        rlk_wg_rand(ctx->wg, ctx->wx, ctx->wy, 2) < 0.3f) {
        out->type = TILE_ROCK;
        out->flags = 0x01;
        out->variant = rock_variant(ctx, (const tilemap_gen_t *)data, ctx->depth);
    }
}

static void
write_desert(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (write_land_water(ctx, out)) return;
    if (write_coast(ctx, data, out)) return;
    out->type = TILE_SAND;
    out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
}

static void
write_steppe(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (write_land_water(ctx, out)) return;
    if (write_coast(ctx, data, out)) return;
    /* Écotone: near a desert border, sand spills over (50%) */
    if (ctx->zone2 == Z_DESERT && ctx->edge > 0.5f &&
        rlk_wg_rand(ctx->wg, ctx->wx, ctx->wy, 22) < 0.5f) {
        out->type = TILE_SAND;
        out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
        return;
    }
    out->type = TILE_GRASS;
    /* sparse: light variants, tufts only */
    out->variant = (uint8_t)(ctx->field[F_DETAIL] * 1.5f);
    land_rock_outcrop(ctx, data, out);
}

static void
write_plains(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (write_land_water(ctx, out)) return;
    if (write_coast(ctx, data, out)) return;
    /* Écotone: near a forest border, trees spill over (30%) */
    if (ctx->zone2 == Z_FOREST && ctx->edge > 0.5f &&
        rlk_wg_rand(ctx->wg, ctx->wx, ctx->wy, 21) < 0.3f) {
        out->type = TILE_TREE;
        out->flags = 0x01;
        out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
        return;
    }
    out->type = TILE_GRASS;
    /* Variant: mix detail noise with depth (darker green in the biome centre) */
    out->variant = (uint8_t)((ctx->field[F_DETAIL] * 0.6f + ctx->depth * 0.4f) * 3.0f);
    land_rock_outcrop(ctx, data, out);
}

static void
write_forest(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (write_land_water(ctx, out)) return;
    if (write_coast(ctx, data, out)) return;
    /* Tree density */
    float density = (0.3f + ctx->depth * 0.5f) * (0.5f + ctx->field[F_DETAIL]);
    /* Variant: mix detail noise with depth (darker in the forest centre) */
    out->variant = (uint8_t)((ctx->field[F_DETAIL] * 0.5f + ctx->depth * 0.5f) * 3.0f);
    if (rlk_wg_rand(ctx->wg, ctx->wx, ctx->wy, 1) < density) {
        out->type = TILE_TREE;  /* hot + wet: broadleaf */
        out->flags = 0x01;
    } else {
        out->type = TILE_FLOOR;
    }
}

static void
write_tundra(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (write_land_water(ctx, out)) return;
    if (write_coast(ctx, data, out)) return;
    /* cold + dry: patchy snow over pale grass */
    if (ctx->field[F_DETAIL] > 0.5f) {
        out->type = TILE_SNOW;
        out->flags = 0x01;
        out->variant = 0;
    } else {
        out->type = TILE_GRASS;
        out->variant = 0;
    }
}

static void
write_taiga(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (write_land_water(ctx, out)) return;
    if (write_coast(ctx, data, out)) return;
    /* Écotone: near a tundra border, snow creeps down (50%) */
    if (ctx->zone2 == Z_TUNDRA && ctx->edge > 0.5f &&
        rlk_wg_rand(ctx->wg, ctx->wx, ctx->wy, 23) < 0.5f) {
        out->type = TILE_SNOW;
        out->flags = 0x01;
        out->variant = 0;
        return;
    }
    /* cold + wet: conifers over forest floor */
    float density = (0.3f + ctx->depth * 0.5f) * (0.5f + ctx->field[F_DETAIL]);
    out->variant = (uint8_t)((ctx->field[F_DETAIL] * 0.5f + ctx->depth * 0.5f) * 3.0f);
    if (rlk_wg_rand(ctx->wg, ctx->wx, ctx->wy, 5) < density) {
        out->type = TILE_PINE;
        out->flags = 0x01;
    } else {
        out->type = TILE_FLOOR;
    }
}

static void
write_mountain(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (write_land_water(ctx, out)) return;
    out->type = TILE_ROCK;
    out->flags = 0x01;
    /* Variant: lithology-based (basalt dark, granite light), depth-mixed */
    out->variant = rock_variant(ctx, (const tilemap_gen_t *)data, ctx->depth);
}

static void
write_snow(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    if (write_land_water(ctx, out)) return;
    /* Snow line = f(temperature): 0.85 when cold (snow at sea level at the
       poles), 1.00 when hot (~5000 m at the equator). Smoothstep over 0.05
       of elevation: patchy snow near the line, pure white above. */
    float snow_line = 0.85f + 0.15f * ctx->field[F_TEMP];
    float t = (ctx->field[F_ELEV] - snow_line) / 0.05f;
    if (t <= 0.0f) {
        out->type = TILE_ROCK;
        out->flags = 0x01;
        out->variant = rock_variant(ctx, (const tilemap_gen_t *)data, ctx->depth);
        return;
    }
    float s = t >= 1.0f ? 1.0f : t * t * (3.0f - 2.0f * t);
    out->type = TILE_SNOW;
    out->flags = 0x01;
    out->variant = (uint8_t)((1.0f - s) * 3.0f);  /* 3 patchy -> 0 pure */
}

/* --- POI: terrain-constrained detectors + multi-tile content ---------------
   The stock rlk_wg_poi stages (7-10) cover field/grid/drain constraints.
   Stage 11 adds kinds constrained by zone / slope / river / geology, all
   readable from the per-tile ctx and the geology buffers (stage 6 runs
   before, per tile). */

typedef struct {
    uint32_t type;
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

/* Stage 11: terrain-constrained POI detectors (table-driven). */
static void
poi_terrain_stage(rlk_wg_ctx_t *ctx, const void *data) {
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

/* Multi-tile content inside each POI bbox (poi.h contract: a generator
   writes only inside the bbox of its POI; on overlap the first POI in
   emission order wins). Surface sites are stamped here; cave/dungeon bboxes
   are only reserved (their content is generated by local generators, e.g.
   demo_world.c). Water tiles are never overwritten by land content. */
static int
poi_is_water(uint8_t type) {
    return type == TILE_WATER || type == TILE_RIVER || type == TILE_LAKE;
}

static void
stamp_poi(tilemap_gen_t *gen, const rlk_poi_t *p) {
    rlk_rect_t map_r = { 0, 0, TM_W, TM_H };
    rlk_rect_t box;
    if (!rlk_rect_intersect(&box, &p->bbox, &map_r))
        return;
    /* First POI processed wins: skip if any tile of the bbox is reserved. */
    for (int y = box.y; y < box.y + box.h; y++)
        for (int x = box.x; x < box.x + box.w; x++)
            if (gen->poi_res[(size_t)y * TM_W + (size_t)x] != 0xFFu)
                return;

    const int hs = p->bbox.w / 2;
    for (int y = box.y; y < box.y + box.h; y++) {
        for (int x = box.x; x < box.x + box.w; x++) {
            gen->poi_res[(size_t)y * TM_W + (size_t)x] = (uint8_t)p->type;
            rlk_tile_t *t = rlk_tilemap_get(&gen->tm, x, y);
            float r = rlk_wg_rand(&gen->wg, x, y,
                                     rlk_hash_sub(p->seed, 0x57A9ULL));
            int dx = x - (p->x - hs), dy = y - (p->y - hs);  /* bbox-local */
            int on_ring = (dx == 0 || dy == 0 ||
                           dx == p->bbox.w - 1 || dy == p->bbox.h - 1);
            int is_anchor = (x == p->x && y == p->y);
            switch (p->type) {
            case POI_VILLAGE:
                if (poi_is_water(t->type)) break;
                t->type = TILE_FLOOR; t->flags = 0; t->variant = 0;
                if (fabsf((float)dx - (float)hs) <= 1.5f && fabsf((float)dy - (float)hs) <= 1.5f) {
                    t->type = TILE_PLAZA;
                } else if (is_anchor || r < 0.20f) {
                    t->type = TILE_HOUSE; t->flags = 0x01;
                    t->variant = (uint8_t)(r * 12.0f);
                }
                break;
            case POI_HAMLET:
                if (poi_is_water(t->type)) break;
                t->type = TILE_FLOOR; t->flags = 0; t->variant = 0;
                if (is_anchor || r < 0.35f) {
                    t->type = TILE_HOUSE; t->flags = 0x01;
                    t->variant = (uint8_t)(r * 8.0f);
                }
                break;
            case POI_FORT:
                if (poi_is_water(t->type)) break;
                if (on_ring) {
                    t->type = TILE_ROCK; t->flags = 0x01;
                    t->variant = 2;
                } else {
                    t->type = TILE_FLOOR; t->flags = 0; t->variant = 0;
                    if (is_anchor || r < 0.15f) {
                        t->type = TILE_HOUSE; t->flags = 0x01;
                        t->variant = 1;
                    }
                }
                break;
            case POI_MILL:
                if (poi_is_water(t->type)) break;
                t->type = TILE_FLOOR; t->flags = 0; t->variant = 0;
                if (is_anchor) { t->type = TILE_HOUSE; t->flags = 0x01; t->variant = 2; }
                break;
            case POI_PORT:
                if (poi_is_water(t->type)) {
                    t->type = TILE_DOCK; t->flags = 0x02; t->variant = (uint8_t)(r * 4.0f);
                } else if (is_anchor) {
                    t->type = TILE_HOUSE; t->flags = 0x01; t->variant = 3;
                }
                break;
            case POI_LIGHTHOUSE:
                if (!poi_is_water(t->type) && is_anchor) {
                    t->type = TILE_HOUSE; t->flags = 0x01; t->variant = 3;
                }
                break;
            case POI_TOWER:
                if (!poi_is_water(t->type) && is_anchor) {
                    t->type = TILE_HOUSE; t->flags = 0x01; t->variant = 2;
                }
                break;
            case POI_MINE:
                if (poi_is_water(t->type)) break;
                if (is_anchor) {
                    t->type = TILE_HOUSE; t->flags = 0x01; t->variant = 0;
                } else if (r < 0.5f) {
                    t->type = TILE_ROCK; t->flags = 0x01; t->variant = 3;
                } else {
                    t->type = TILE_FLOOR; t->flags = 0; t->variant = 1;
                }
                break;
            default:
                break;   /* cave/dungeon: reserved only, local gen owns them */
            }
        }
    }
}

/* Stamp the content of every detected POI, in emission order. */
static void
poi_content_pass(tilemap_gen_t *gen) {
    for (int i = 0; i < TM_W * TM_H; i++)
        gen->poi_res[i] = 0xFFu;
    for (int i = 0; i < gen->pois.n; i++)
        stamp_poi(gen, &gen->pois.v[i]);
}

/* --- Cosmetic pass (post-fill, post-POI content) ---------------------------
   Karst dolines on limestone lowlands, sand deltas where a river meets the
   sea, oxbow ponds next to carved (meandering) reaches, and karst cave
   entrances (a cave POI anchor on limestone becomes a sinkhole). Never
   overwrites POI-reserved tiles (poi_res) and never turns land into water
   except oxbow ponds on plain land. */

#define KARST_DENS    0.012f  /* doline probability per limestone lowland tile */
#define DELTA_RADIUS  2       /* sand-fan reach around a river mouth (tiles) */
#define DELTA_DENS    0.70f   /* probability to deposit sand on a shallow tile */
#define OXBOW_DENS    0.03f   /* pond probability on land next to a carved river */

static int
cosmetic_land(uint8_t type) {
    return type != TILE_VOID && type != TILE_WATER &&
           type != TILE_RIVER && type != TILE_LAKE;
}

static void
cosmetic_pass(tilemap_gen_t *gen) {
    /* Karst dolines on sedimentary (limestone) lowlands */
    for (int y = 0; y < TM_H; y++) {
        for (int x = 0; x < TM_W; x++) {
            int idx = y * TM_W + x;
            if (gen->litho_buf[idx] != LITHO_SEDIMENT)
                continue;
            float e = gen->final_elev[idx];
            if (e < ELEV_BEACH + 0.02f || e > ELEV_LOWLAND)
                continue;
            rlk_tile_t *t = rlk_tilemap_get(&gen->tm, x, y);
            if (!cosmetic_land(t->type) || gen->poi_res[idx] != 0xFFu)
                continue;
            float r = rlk_wg_rand(&gen->wg, x, y, 0xCA257ULL);
            if (r < KARST_DENS) {
                t->type = TILE_SINKHOLE;
                t->flags = 0x01;
                t->variant = (uint8_t)(r * 80.0f) & 3u;
            }
        }
    }

    /* Deltas: a river tile at the shoreline with a sea neighbour deposits a
       sand fan over the shallow water (the delta progrades into the sea) */
    for (int y = 0; y < TM_H; y++) {
        for (int x = 0; x < TM_W; x++) {
            int idx = y * TM_W + x;
            if (gen->river_mask_buf[idx] <= 0.5f || gen->final_elev[idx] > ELEV_BEACH)
                continue;
            int mouth = 0;
            for (int dy = -1; dy <= 1 && !mouth; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= TM_W || ny < 0 || ny >= TM_H)
                        continue;
                    if (gen->final_elev[ny * TM_W + nx] < ELEV_SHALLOW) {
                        mouth = 1;
                        break;
                    }
                }
            if (!mouth)
                continue;
            for (int dy = -DELTA_RADIUS; dy <= DELTA_RADIUS; dy++) {
                for (int dx = -DELTA_RADIUS; dx <= DELTA_RADIUS; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= TM_W || ny < 0 || ny >= TM_H)
                        continue;
                    int nidx = ny * TM_W + nx;
                    float ne = gen->final_elev[nidx];
                    if (ne < ELEV_DEEP || ne >= ELEV_SHALLOW)
                        continue;
                    rlk_tile_t *nt = rlk_tilemap_get(&gen->tm, nx, ny);
                    if (nt->type != TILE_WATER || gen->poi_res[nidx] != 0xFFu)
                        continue;
                    if (rlk_wg_rand(&gen->wg, nx, ny, 0xDE17AULL) < DELTA_DENS) {
                        nt->type = TILE_SAND;
                        nt->flags = 0;
                        nt->variant = (uint8_t)(rlk_wg_rand(&gen->wg, nx, ny, 0x5A11ULL) * 3.0f);
                    }
                }
            }
        }
    }

    /* Oxbow ponds: plain land next to a carved river reach becomes a pond
       (meander cut-off) */
    for (int y = 0; y < TM_H; y++) {
        for (int x = 0; x < TM_W; x++) {
            int idx = y * TM_W + x;
            if (gen->river_mask_buf[idx] <= 0.5f || gen->river_carve_buf[idx] <= 0.0f)
                continue;
            static const int d4[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
            for (int k = 0; k < 4; k++) {
                int nx = x + d4[k][0], ny = y + d4[k][1];
                if (nx < 0 || nx >= TM_W || ny < 0 || ny >= TM_H)
                    continue;
                int nidx = ny * TM_W + nx;
                if (gen->final_elev[nidx] >= ELEV_LOWLAND || gen->poi_res[nidx] != 0xFFu)
                    continue;
                rlk_tile_t *nt = rlk_tilemap_get(&gen->tm, nx, ny);
                if (!cosmetic_land(nt->type))
                    continue;
                if (rlk_wg_rand(&gen->wg, nx, ny, 0x0B0CULL) < OXBOW_DENS) {
                    nt->type = TILE_LAKE;
                    nt->flags = 0x02;
                    nt->variant = 0;
                    break;
                }
            }
        }
    }

    /* Karst caves: a cave POI anchor on limestone reads as a sinkhole entrance */
    for (int i = 0; i < gen->pois.n; i++) {
        const rlk_poi_t *p = &gen->pois.v[i];
        if (p->type != POI_CAVE || p->x < 0 || p->x >= TM_W || p->y < 0 || p->y >= TM_H)
            continue;
        int idx = p->y * TM_W + p->x;
        if (gen->litho_buf[idx] != LITHO_SEDIMENT)
            continue;
        rlk_tile_t *t = rlk_tilemap_get(&gen->tm, p->x, p->y);
        if (cosmetic_land(t->type)) {
            t->type = TILE_SINKHOLE;
            t->flags = 0x01;
            t->variant = 0;
        }
    }
}

/* --- GENERATION ------------------------------------------------------------ */

void
tilemap_gen_generate(tilemap_gen_t *gen, uint64_t seed) {
    assert(gen);
    gen->seed = seed;

    rlk_tilemap_init(&gen->tm, TM_W, TM_H, gen->buffer, sizeof(gen->buffer));

    /* --- wg field config (before grid fill: relief_noise seeding needs field seeds) --- */
    rlk_wg_t *wg = &gen->wg;
    wg->seed = seed;
    wg->nstages = 0;
    wg->nsampled = F_NSAMPLED;
    wg->nderived = F_NDERIVED;
    wg->fields[F_CONT]   = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 1, .octaves = 3, .freq = 0.012f, .gain = 0.5f };
    wg->fields[F_HILLS]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 2, .octaves = 4, .freq = 0.050f, .gain = 0.5f };
    wg->fields[F_RIDGE]  = (rlk_wg_field_t){ .kind = RLK_WG_RIDGE, .seed = 3, .octaves = 4, .freq = 0.030f, .gain = 0.5f };
    wg->fields[F_WARP_X] = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 4, .octaves = 2, .freq = 0.025f, .gain = 0.5f };
    wg->fields[F_WARP_Y] = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 5, .octaves = 2, .freq = 0.025f, .gain = 0.5f };
    wg->fields[F_FJORD]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 6, .octaves = 2, .freq = 0.040f, .gain = 0.5f };
    wg->fields[F_TEMP]   = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 7, .octaves = 4, .freq = 0.040f, .gain = 0.5f };
    wg->fields[F_DETAIL] = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 8, .octaves = 2, .freq = 0.050f, .gain = 0.5f };
    wg->fields[F_WIG_X]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 9, .octaves = 2, .freq = 0.100f, .gain = 0.5f };
    wg->fields[F_WIG_Y]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 10, .octaves = 2, .freq = 0.120f, .gain = 0.5f };
    wg->fields[F_CREEK]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 11, .octaves = 2, .freq = 0.025f, .gain = 0.5f };
    wg->fields[F_HUM]    = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 12, .octaves = 4, .freq = 0.040f, .gain = 0.5f };

    /* --- Provinces: stock Voronoi modulator biasing the sampled fields before
       relief/rivers. Type 0 is neutral; 1..4 are ARID/LUSH/COLD/ISLES.
       cell 64 tiles, blend 16 tiles: macro-zones a few cells wide. */
    gen->prov_cfg = (rlk_wg_modulator_cfg_t){
        .ntypes = 5,
        .cell = 64.0f,
        .blend = 16.0f,
        .salt = 0x9200ULL,
    };
    gen->prov_cfg.types[1].bias[F_HUM]  = -0.14f;  /* ARID: dry + hot   */
    gen->prov_cfg.types[1].bias[F_TEMP] = +0.08f;
    gen->prov_cfg.types[2].bias[F_HUM]  = +0.14f;  /* LUSH: wet         */
    gen->prov_cfg.types[3].bias[F_TEMP] = -0.16f;  /* COLD: cold        */
    gen->prov_cfg.types[4].bias[F_CONT] = -0.10f;  /* ISLES: more sea   */
    gen->prov_mod = rlk_wg_modulator(&gen->prov_cfg);

    /* --- Coarse elevation grid: seed noise like wg, fill with relief terrain ---
       The drain grid must see the SAME terrain the relief stage displays,
       otherwise rivers follow a different landscape than what is shown. */
    for (int i = 0; i < F_NSAMPLED; i++) {
        uint64_t s = rlk_hash_sub(seed, 0xF1E1D000ULL + (uint64_t)i);
        rlk_noise_seed(&gen->relief_noise[i], rlk_hash_sub(s, wg->fields[i].seed));
    }
    /* Light geology noise (demo-owned, not wg fields) */
    for (int i = 0; i < 3; i++)
        rlk_noise_seed(&gen->geo_noise[i], rlk_hash_sub(seed, 0x6E0600ULL + (uint64_t)i));
    relief_elev_ctx_t elev_ctx = {
        .gen = gen,
        .wg = wg,
        .noise = gen->relief_noise,
        .warp = 8.0f,
        .hills_amp = 0.30f,
        .mountain_lo = 0.60f,
        .mountain_hi = 0.80f,
        .mountain_amp = 0.45f,
    };
    gen->elev_grid = (rlk_grid_t){ .lattice = { .w = GRID_W, .h = GRID_H, .cellsize = GRID_CELL,
                                    .origin_x = GRID_ORIGIN_X, .origin_y = GRID_ORIGIN_Y }, .v = gen->elev_buf };
    for (int j = 0; j < GRID_H; j++)
        for (int i = 0; i < GRID_W; i++) {
            float cx, cy;
            rlk_grid_center(&gen->elev_grid, i, j, &cx, &cy);
            gen->elev_buf[(size_t)j * (size_t)GRID_W + (size_t)i] = relief_elev_fn(cx, cy, &elev_ctx);
        }

    /* --- Drain: depression filling + D8 drainage + accumulation --- */
    gen->drain = (rlk_drain_t){ .lattice = gen->elev_grid.lattice,
                              .filled = gen->drain_filled, .dir = gen->drain_dir,
                              .acc = gen->drain_acc, .order = gen->drain_order };
    rlk_err_t derr = rlk_drain_build(&gen->drain, &gen->elev_grid, SEA_LEVEL, gen->drain_scratch);
    assert(derr == RLK_OK);

    /* --- Lake grid: filled depression depths, filtered by outlet accumulation ---
       Lakes whose outflow drains fewer than RIVER_ACC_MIN cells are dropped:
       they would sit with no visible outlet. */
    gen->lake_grid = (rlk_grid_t){ .lattice = { .w = GRID_W, .h = GRID_H, .cellsize = GRID_CELL,
                                    .origin_x = GRID_ORIGIN_X, .origin_y = GRID_ORIGIN_Y }, .v = gen->lake_buf };
    for (int j = 0; j < GRID_H; j++)
        for (int i = 0; i < GRID_W; i++)
            gen->lake_buf[(size_t)j * (size_t)GRID_W + (size_t)i] =
                rlk_drain_lake_depth(&gen->drain, &gen->elev_grid, SEA_LEVEL, i, j);
    /* Filter: drop lake cells whose outlet accumulation < RIVER_ACC_MIN */
    for (int c = 0; c < GRID_W * GRID_H; c++) {
        if (gen->lake_buf[c] <= 0.0f) continue;
        int cur = c;
        for (;;) {
            if (gen->drain.dir[cur] == RLK_DRAIN_OUTLET) break;
            int dx, dy;
            rlk_drain_step(gen->drain.dir[cur], &dx, &dy);
            const int nx = (cur % GRID_W) + dx, ny = (cur / GRID_W) + dy;
            if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) break;
            const int nc = ny * GRID_W + nx;
            if (gen->lake_buf[nc] <= 0.0f) break;
            cur = nc;
        }
        if (gen->drain.acc[cur] < RIVER_ACC_MIN) gen->lake_buf[c] = 0.0f;
    }

    /* --- Hydro: derived fields on the drain grid (slope, Strahler, speed) --- */
    gen->hydro = (rlk_hydro_t){ .lattice = gen->drain.lattice,
                                .slope = gen->hydro_slope,
                                .strahler = gen->hydro_strahler,
                                .speed = gen->hydro_speed };
    rlk_hydro_build(&gen->hydro, &gen->drain, RIVER_ACC_MIN, gen->hydro_scratch);

    /* --- wg stages: provinces + drain + hydro + rivers + strahler + relief + POI --- */

    /* Stage 0: provinces (F_HUM offset decorrelation + Voronoi modulator).
       Reads/writes all sampled fields (the modulator part). */
    rlk_wg_add(wg, (rlk_wg_stage_t){ .fn = province_stage, .data = gen,
                                      .reads = { .sampled = RLK_WG_SLOTS_ALL },
                                      .writes = { .sampled = RLK_WG_SLOTS_ALL } });

    /* Stage 1: wg_drain — inject accumulation + lake depth */
    gen->drain_stage = (rlk_wg_drain_cfg_t){
        .drain = &gen->drain,
        .elev = &gen->elev_grid,
        .sea = SEA_LEVEL,
        .slot_acc  = RLK_WG_DRAIN_NONE,   /* acc read straight from the drain grid */
        .slot_dir  = RLK_WG_DRAIN_NONE,
        .slot_lake = D_LAKE,
        .acc_scale = 1.0f,
    };
    rlk_wg_add(wg, rlk_wg_drain(&gen->drain_stage));

    /* Stage 2: wg_hydro — inject slope / Strahler order / speed */
    gen->hydro_stage = (rlk_wg_hydro_cfg_t){
        .hydro = &gen->hydro,
        .slot_slope = D_SLOPE,
        .slot_strahler = D_STRAHLER,
        .slot_speed = D_SPEED,
    };
    rlk_wg_add(wg, rlk_wg_hydro(&gen->hydro_stage));

    /* Stage 3: wg_rivers — project drainage into per-tile river masks.
       Channel width follows the drain SDF (~log2(acc), capped at cellsize);
       mask fades 1 tile past the bank, valley reach = 2 * cellsize.
       Meanders from F_WIG_X/Y, creeks from F_CREEK, lakes from the filtered grid. */
    gen->rivers_stage = (rlk_wg_rivers_cfg_t){
        .drain = &gen->drain,
        .elev = &gen->elev_grid,
        .sea = SEA_LEVEL,
        .acc_min = RIVER_ACC_MIN,
        .bank_fade = 1.0f,
        .valley_mult = 2.0f,
        .carve = 0.03f,
        .slot_mask = D_RIVER_MASK,
        .slot_carve = D_RIVER_CARVE,
        .lake_depth = &gen->lake_grid,
        .lake_min_depth = LAKE_MIN_DEPTH,
        .f_wig_x = F_WIG_X,
        .f_wig_y = F_WIG_Y,
        .wig_tiles = 3.0f,
        .f_creek = F_CREEK,
        .creek_width = 1.0f,
        .creek_carve = 0.02f,
    };
    rlk_wg_add(wg, rlk_wg_rivers(&gen->rivers_stage));

    /* Stage 4: Strahler width — narrow headwaters, wide big rivers */
    rlk_wg_add(wg, (rlk_wg_stage_t){ .fn = strahler_stage, .data = gen,
                                      .reads = { .derived = RLK_WG_SLOT(D_RIVER_MASK) | RLK_WG_SLOT(D_STRAHLER) },
                                      .writes = { .derived = RLK_WG_SLOT(D_RIVER_MASK) } });

    /* Stage 5: wg_relief — standardised elevation (domain warp, fjords, lapse) */
    gen->relief_stage = (rlk_wg_relief_cfg_t){
        .f_cont = F_CONT,
        .f_hills = F_HILLS,
        .f_ridge = F_RIDGE,
        .f_warp_x = F_WARP_X,
        .f_warp_y = F_WARP_Y,
        .f_fjord = F_FJORD,
        .f_temp = F_TEMP,
        .f_river = RLK_WG_RELIEF_NONE,  /* rivers come from wg_rivers */
        .slot_elev = D_ELEV,
        .slot_river = RLK_WG_RELIEF_NONE,
        .river_carve_field = D_RIVER_CARVE,  /* read carve from wg_rivers */
        .sea = SEA_LEVEL,
        .warp = 8.0f,
        .hills_amp = 0.30f,
        .mountain_lo = 0.60f,
        .mountain_hi = 0.80f,
        .mountain_amp = 0.45f,
        .lapse = 0.25f,
        .river_width_tiles = 2.0f,
        .river_carve = 0.03f,
        .relief_span = 0.40f,
        .fjord_dir = { 0.8f, 0.6f },
        .fjord_stretch = 0.25f,
        .fjord_width = 0.12f,
        .fjord_depth = 0.50f,
        .fjord_steep = 3.0f,
        .fjord_reach = 0.15f,
        .fjord_cold0 = 0.45f,
        .fjord_cold1 = 0.65f,
    };
    gen->relief_mod = rlk_wg_relief(&gen->relief_stage);
    /* Wrapper around the stock relief stage, adding the rain shadow on F_HUM.
       data must be the tilemap_gen_t (relief_rain_stage reads gen->relief_mod),
       not the relief cfg that the stock stage carries. */
    rlk_wg_stage_t rain = gen->relief_mod;
    rain.fn = relief_rain_stage;
    rain.data = gen;
    rain.reads.sampled |= RLK_WG_SLOT(F_HUM);
    rain.writes.sampled |= RLK_WG_SLOT(F_HUM);
    rlk_wg_add(wg, rain);

    /* Stage 6: geology — structural bias on the final elevation + lithology */
    rlk_wg_add(wg, (rlk_wg_stage_t){ .fn = geology_stage, .data = gen,
                                      .reads = { .derived = RLK_WG_SLOT(D_ELEV) },
                                      .writes = { .derived = RLK_WG_SLOT(D_ELEV) } });

    /* Stage 7-10: POI detectors */
    gen->pois = (rlk_poi_list_t){ gen->poi_buf, 0, 256 };

    /* Cave: on hills/mountains, high detail noise */
    gen->poi_cave_cfg = (rlk_wg_poi_cfg_t){
        .list = &gen->pois,
        .type = POI_CAVE,
        .salt = 0xCAFEULL,
        .f_sampled = F_DETAIL,
        .threshold = 0.70f,
        .density = 0.003f,
        .size = 24,
        .grid = &gen->elev_grid,
        .grid_min = 0.40f,
        .grid_max = 0.85f,
        .min_separation = 24,
    };
    rlk_wg_add(wg, rlk_wg_poi(&gen->poi_cave_cfg));

    /* Dungeon: near rivers */
    gen->poi_dungeon_cfg = (rlk_wg_poi_cfg_t){
        .list = &gen->pois,
        .type = POI_DUNGEON,
        .salt = 0xD0D0ULL,
        .f_sampled = F_DETAIL,
        .threshold = 0.50f,
        .density = 0.002f,
        .size = 32,
        .drain = &gen->drain,
        .min_acc = 30.0f,
        .min_separation = 32,
    };
    rlk_wg_add(wg, rlk_wg_poi(&gen->poi_dungeon_cfg));

    /* Village: warm lowland plains only (temperature + altitude constraints) */
    gen->poi_village_cfg = (rlk_wg_poi_cfg_t){
        .list = &gen->pois,
        .type = POI_VILLAGE,
        .salt = 0xA11CEULL,
        .f_sampled = F_TEMP,
        .threshold = 0.45f,
        .density = 0.006f,
        .size = 16,
        .grid = &gen->elev_grid,
        .grid_min = 0.35f,
        .grid_max = 0.50f,
        .min_separation = 16,
    };
    rlk_wg_add(wg, rlk_wg_poi(&gen->poi_village_cfg));

    /* Tower: on hills and peaks */
    gen->poi_tower_cfg = (rlk_wg_poi_cfg_t){
        .list = &gen->pois,
        .type = POI_TOWER,
        .salt = 0x70E1ULL,
        .f_sampled = F_DETAIL,
        .threshold = 0.60f,
        .density = 0.002f,
        .size = 8,
        .grid = &gen->elev_grid,
        .grid_min = 0.70f,
        .grid_max = 1.0f,
        .min_separation = 8,
    };
    rlk_wg_add(wg, rlk_wg_poi(&gen->poi_tower_cfg));

    /* Stage 11: terrain-constrained POI kinds (zone/slope/river/geology) */
    rlk_wg_add(wg, (rlk_wg_stage_t){ .fn = poi_terrain_stage, .data = gen,
                                      .reads = { .sampled = RLK_WG_SLOT(F_TEMP) | RLK_WG_SLOT(F_HUM),
                                                 .derived = RLK_WG_SLOT(D_ELEV) | RLK_WG_SLOT(D_RIVER_MASK) | RLK_WG_SLOT(D_SLOPE) } });

    /* Axes: elevation (relief output), temperature (lapse-cooled), humidity */
    wg->naxes = 3;
    wg->axes[0] = (rlk_wg_axis_t){ .field = F_ELEV,  .jitter_field = F_DETAIL, .jitter_amp = 0.03f };
    wg->axes[1] = (rlk_wg_axis_t){ .field = F_TEMP, .jitter_field = F_DETAIL, .jitter_amp = 0.03f };
    wg->axes[2] = (rlk_wg_axis_t){ .field = F_HUM,  .jitter_field = F_DETAIL, .jitter_amp = 0.03f };

    /* Zones: elevation slices on axis 0; the lowland band splits into the 6
       Köppen biomes on (temp, hum). Unbound axes use +/- RLK_WG_OPEN. */
    wg->nzones = Z_COUNT;

    /* Setup capture wrappers: each zone writes through capture_write */
    gen->cap[Z_DEEP]     = (tilemap_gen_capture_t){ .gen = gen, .write = write_deep,     .real_data = gen };
    gen->cap[Z_SHALLOW]  = (tilemap_gen_capture_t){ .gen = gen, .write = write_shallow,  .real_data = gen };
    gen->cap[Z_BEACH]    = (tilemap_gen_capture_t){ .gen = gen, .write = write_beach,    .real_data = gen };
    gen->cap[Z_DESERT]   = (tilemap_gen_capture_t){ .gen = gen, .write = write_desert,   .real_data = gen };
    gen->cap[Z_STEPPE]   = (tilemap_gen_capture_t){ .gen = gen, .write = write_steppe,   .real_data = gen };
    gen->cap[Z_PLAINS]   = (tilemap_gen_capture_t){ .gen = gen, .write = write_plains,   .real_data = gen };
    gen->cap[Z_FOREST]   = (tilemap_gen_capture_t){ .gen = gen, .write = write_forest,   .real_data = gen };
    gen->cap[Z_TUNDRA]   = (tilemap_gen_capture_t){ .gen = gen, .write = write_tundra,   .real_data = gen };
    gen->cap[Z_TAIGA]    = (tilemap_gen_capture_t){ .gen = gen, .write = write_taiga,    .real_data = gen };
    gen->cap[Z_MOUNTAIN] = (tilemap_gen_capture_t){ .gen = gen, .write = write_mountain, .real_data = gen };
    gen->cap[Z_SNOW]     = (tilemap_gen_capture_t){ .gen = gen, .write = write_snow,     .real_data = gen };

#define Z_OPEN_LO (-RLK_WG_OPEN)
#define Z_OPEN_HI ( RLK_WG_OPEN)
    wg->zones[Z_DEEP]     = (rlk_wg_zone_t){ .lo = {0.0f,        Z_OPEN_LO, Z_OPEN_LO}, .hi = {ELEV_DEEP,    Z_OPEN_HI, Z_OPEN_HI}, .write = capture_write, .data = &gen->cap[Z_DEEP] };
    wg->zones[Z_SHALLOW]  = (rlk_wg_zone_t){ .lo = {ELEV_DEEP,   Z_OPEN_LO, Z_OPEN_LO}, .hi = {ELEV_SHALLOW, Z_OPEN_HI, Z_OPEN_HI}, .write = capture_write, .data = &gen->cap[Z_SHALLOW] };
    wg->zones[Z_BEACH]    = (rlk_wg_zone_t){ .lo = {ELEV_SHALLOW, Z_OPEN_LO, Z_OPEN_LO}, .hi = {ELEV_BEACH,  Z_OPEN_HI, Z_OPEN_HI}, .write = capture_write, .data = &gen->cap[Z_BEACH] };
    /* Lowland: 6 biomes on (temp, hum) */
    wg->zones[Z_DESERT]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   TEMP_HOT,  0.0f},     .hi = {ELEV_LOWLAND, Z_OPEN_HI, HUM_DRY},   .write = capture_write, .data = &gen->cap[Z_DESERT] };
    wg->zones[Z_FOREST]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   TEMP_HOT,  HUM_DRY},  .hi = {ELEV_LOWLAND, Z_OPEN_HI, Z_OPEN_HI}, .write = capture_write, .data = &gen->cap[Z_FOREST] };
    wg->zones[Z_STEPPE]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   TEMP_COLD, 0.0f},     .hi = {ELEV_LOWLAND, TEMP_HOT,  HUM_DRY},   .write = capture_write, .data = &gen->cap[Z_STEPPE] };
    wg->zones[Z_PLAINS]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   TEMP_COLD, HUM_DRY},  .hi = {ELEV_LOWLAND, TEMP_HOT,  Z_OPEN_HI}, .write = capture_write, .data = &gen->cap[Z_PLAINS] };
    wg->zones[Z_TUNDRA]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   Z_OPEN_LO, 0.0f},     .hi = {ELEV_LOWLAND, TEMP_COLD, HUM_DRY},   .write = capture_write, .data = &gen->cap[Z_TUNDRA] };
    wg->zones[Z_TAIGA]    = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   Z_OPEN_LO, HUM_DRY},  .hi = {ELEV_LOWLAND, TEMP_COLD, Z_OPEN_HI}, .write = capture_write, .data = &gen->cap[Z_TAIGA] };
    wg->zones[Z_MOUNTAIN] = (rlk_wg_zone_t){ .lo = {ELEV_LOWLAND, Z_OPEN_LO, Z_OPEN_LO}, .hi = {ELEV_MOUNTAIN, Z_OPEN_HI, Z_OPEN_HI}, .write = capture_write, .data = &gen->cap[Z_MOUNTAIN] };
    wg->zones[Z_SNOW]     = (rlk_wg_zone_t){ .lo = {ELEV_MOUNTAIN, Z_OPEN_LO, Z_OPEN_LO}, .hi = {1.0f,         Z_OPEN_HI, Z_OPEN_HI}, .write = capture_write, .data = &gen->cap[Z_SNOW] };
#undef Z_OPEN_LO
#undef Z_OPEN_HI

    wg->depth_scale = 0.15f;
    wg->transition_width = 0.10f;

    assert(rlk_wg_init(wg) == RLK_OK);
    rlk_wg_fill(wg, 0, 0, &gen->tm);

    /* Multi-tile content inside each POI bbox (first POI wins on overlap) */
    poi_content_pass(gen);

    /* Cosmetic: karst dolines, river-mouth deltas, oxbow ponds, karst caves */
    cosmetic_pass(gen);
}
