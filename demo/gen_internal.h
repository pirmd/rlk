#ifndef GEN_INTERNAL_H
#define GEN_INTERNAL_H

/*
 * gen_internal - declarations shared between the tilemap_gen demo modules
 * (gen_stages.c, gen_write.c, gen_post.c, gen_stats.c, tilemap_gen.c).
 * Not part of the public demo API (see tilemap_gen.h).
 */

#include "tilemap_gen.h"

/* --- Shared thresholds ---------------------------------------------------
   Elevation thresholds (relief stage output ~[0,1]). Beach is a thin slice
   (0.03, like demo_worldgen) so coasts read naturally. ELEV_LOWLAND 0.70 /
   ELEV_MOUNTAIN 0.85 put mountains at ~15-20% of emerged land (specialist
   review 2026-10-30, geographer P1), not 10%. */
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

#define LAKE_MIN_DEPTH 0.05f    /* min filled depth to be a lake tile (higher = only deeper depressions become lakes) */
#define RIVER_ACC_MIN  20u      /* drainage area (cells) at which a river starts */
#define RAPID_SLOPE    0.02f    /* D8 slope above which a river tile shows rapids */

/* Coast band: beach width depends on local slope (D8). */
#define COAST_EXT     0.06f   /* max inland sand rise */
#define SLOPE_GENTLE  0.01f   /* below: wide beach */
#define SLOPE_STEEP   0.03f   /* above: cliff */

/* --- gen_stages.c: pipeline stages --------------------------------------- */

/* Context for gen_relief_elev_fn: evaluates the wg_relief cfg at an arbitrary
   float position (needs rlk_wg_init first); the province bias goes through
   the stock region stage. */
typedef struct {
    const tilemap_gen_t *gen;           /* for the province region stage */
    const rlk_wg_t *wg;
    const rlk_wg_relief_cfg_t *relief_cfg;  /* single source of relief params */
} gen_relief_ctx_t;

float gen_province_mod_field(const tilemap_gen_t *gen, int f, float value,
                             float x, float y);
void  gen_province_stage(rlk_wg_ctx_t *ctx, const void *data);
void  gen_relief_rain_stage(rlk_wg_ctx_t *ctx, const void *data);
void  gen_strahler_stage(rlk_wg_ctx_t *ctx, const void *data);
float gen_geo_eval(const tilemap_gen_t *gen, float x, float y, float elev,
                   uint8_t *st_out, uint8_t *litho_out);
void  gen_geology_stage(rlk_wg_ctx_t *ctx, const void *data);
float gen_relief_elev_fn(float px, float py, const void *user);
void  gen_poi_terrain_stage(rlk_wg_ctx_t *ctx, const void *data);
void  gen_temp_lat_stage(rlk_wg_ctx_t *ctx, const void *data);

/* --- gen_write.c: zone writers ------------------------------------------- */

typedef void (*gen_zone_write_fn)(const rlk_wg_ctx_t *ctx, const void *data,
                                  rlk_tile_t *out);

/* Capture writer: stores derived fields then dispatches to the zone writer. */
void gen_capture_write(const rlk_wg_ctx_t *ctx, const void *data, rlk_tile_t *out);

/* Fill gen->cap[] from the zone writer table (one entry per zone). */
void gen_setup_captures(tilemap_gen_t *gen);

/* --- gen_post.c: post-fill passes ---------------------------------------- */

void gen_poi_content_pass(tilemap_gen_t *gen);
void gen_cosmetic_pass(tilemap_gen_t *gen);

#endif /* GEN_INTERNAL_H */
