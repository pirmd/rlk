#ifndef RL_REGION_H
#define RL_REGION_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../err.h"
#include "../rng.h"
#include "../noise.h"
#include "../world/tilemap.h"

/*
 * rlk region generation — populates an already-initialized Tilemap from the
 * world seed + region coordinates, deterministically.
 *
 * The caller owns the Tilemap buffer (Pattern A via rlk_tilemap_init). This
 * function only fills tiles + occ_mask; it allocates nothing and uses no
 * scratch (per-tile sampling is local). The same (world_seed, region_x,
 * region_y, width, height) always yields the bit-identical Tilemap, on any
 * platform with uint64_t arithmetic. See docs/ARCHITECTURE.md
 * (RegionGenData is derivable from the seed and never persisted).
 */

typedef struct {
    uint64_t world_seed;
    int32_t  region_x, region_y;   /* region grid coordinates (offset sampling) */
} rlk_region_cfg_t;

/*
 * Tile-type thresholds applied to a normalized elevation/humidity field.
 * Tuning placeholders for Phase 0; richer biomes arrive in later phases.
 *
 *   elevation:  low  -> water
 *               mid  -> sand (shore) -> grass -> dirt
 *               high -> stone (solid wall)
 *   humidity influences grass vs dirt mix; not yet a separate biome.
 */
#define RLK_REG_WATER_LEVEL   0.30f
#define RLK_REG_SAND_LEVEL    0.36f
#define RLK_REG_STONE_LEVEL   0.72f

/* Build a per-region noise seed from the world seed and region coordinates. */
static inline uint64_t
rlk_region_seed(const rlk_region_cfg_t *cfg) {
    assert(cfg);
    uint64_t s = cfg->world_seed
               ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(uint32_t)cfg->region_x)
               ^ (0x6A09E667F3BCC909ULL * (uint64_t)(uint32_t)cfg->region_y);
    return s;
}

/*
 * Populate `tm` deterministically from `cfg`. `tm` must already be
 * initialized (rlk_tilemap_init). Returns RLK_E_CONFIG if cfg is invalid.
 * Sets occ_mask solid for water and stone (impassable base terrain).
 */
static inline rlk_err_t
rlk_region_generate(rlk_tilemap_t *tm, const rlk_region_cfg_t *cfg) {
    assert(tm);
    assert(cfg);
    if (tm->width <= 0 || tm->height <= 0) return RLK_E_CONFIG;

    rlk_noise_t elev, hum;
    uint64_t rs = rlk_region_seed(cfg);
    rlk_noise_seed(&elev, rs);
    rlk_noise_seed(&hum,  rs ^ 0x51ED2408C6C7B47DULL);

    float ts = tm->tile_size;
    int32_t ox = cfg->region_x * tm->width;
    int32_t oy = cfg->region_y * tm->height;

    for (int32_t y = 0; y < tm->height; y++) {
        for (int32_t x = 0; x < tm->width; x++) {
            float wx = (float)(ox + x) * ts;
            float wy = (float)(oy + y) * ts;
            float e = rlk_noise_fbm(&elev, wx, wy, 4, 0.05f, 0.5f);
            float h = rlk_noise_fbm(&hum,  wx, wy, 3, 0.08f, 0.5f);

            rlk_tilecell_t *c = &tm->tiles[(size_t)y * (size_t)tm->width + (size_t)x];
            int solid = 0;
            uint8_t type;

            if (e < RLK_REG_WATER_LEVEL) {
                type = RL_TILE_WATER;
                solid = 1;
            } else if (e < RLK_REG_SAND_LEVEL) {
                type = RL_TILE_SAND;
            } else if (e >= RLK_REG_STONE_LEVEL) {
                type = RL_TILE_STONE;
                solid = 1;
            } else if (h > 0.5f) {
                type = RL_TILE_GRASS;
            } else {
                type = RL_TILE_DIRT;
            }
            c->type  = type;
            c->biome = (uint8_t)(h * 255.0f);   /* humidity snapshot, 0..255 */
            rlk_tilemap_set_occ(tm, x, y, solid);
        }
    }
    return RLK_OK;
}

#endif /* RL_REGION_H */
