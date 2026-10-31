/*
 * gen_write - zone writers of the tilemap_gen demo (see gen_internal.h).
 *
 * One writer per zone (biome): water zones, coast, the 6 Köppen land biomes,
 * mountain and snow. All writers share a land prelude (lake / river / marsh)
 * and the coast band; the capture wrapper stores the derived fields before
 * dispatching. The zone -> writer mapping is a plain table (ZONE_WRITERS),
 * installed on gen->cap[] by gen_setup_captures.
 */

#include "gen_internal.h"

/* Widen the river mask near the coast: up to 2x at the shoreline
   (specialist review 2026-10-30, hydrologist P1: estuary at river mouths).
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

/* Capture writer: stores derived fields then dispatches to the real writer. */
void
gen_capture_write(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out) {
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

/* Varied coasts (specialist review 2026-10-30, geographer P2): beach width =
   f(local D8 slope, F_SLOPE). Gentle slope -> wide beach (sand rises inland),
   steep slope -> cliff (rock). Only in the coastal band
   [ELEV_BEACH, ELEV_BEACH + COAST_EXT] of land zones. */
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
    /* Marsh: floodplain near the coast (review 2026-10-30, hydrologist P1:
       wetland = river edge or valley at low elevation, not an arbitrary rule) */
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
    /* Snow line = f(temperature) (specialist review 2026-10-30, geographer P1):
       0.85 when cold (snow at sea level at the poles), 1.00 when hot (~5000 m
       at the equator). Smoothstep over 0.05 of elevation: patchy snow near
       the line, pure white above. */
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

/* --- Zone writer table ----------------------------------------------------
   One writer per zone; gen_setup_captures installs them on gen->cap[]. */
static const gen_zone_write_fn ZONE_WRITERS[Z_COUNT] = {
    [Z_DEEP]     = write_deep,
    [Z_SHALLOW]  = write_shallow,
    [Z_BEACH]    = write_beach,
    [Z_DESERT]   = write_desert,
    [Z_STEPPE]   = write_steppe,
    [Z_PLAINS]   = write_plains,
    [Z_FOREST]   = write_forest,
    [Z_TUNDRA]   = write_tundra,
    [Z_TAIGA]    = write_taiga,
    [Z_MOUNTAIN] = write_mountain,
    [Z_SNOW]     = write_snow,
};

void
gen_setup_captures(tilemap_gen_t *gen) {
    for (int z = 0; z < Z_COUNT; z++)
        gen->cap[z] = (tilemap_gen_capture_t){ .gen = gen,
                                               .write = ZONE_WRITERS[z],
                                               .real_data = gen };
}
