#ifndef RL_REGION_H
#define RL_REGION_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../err.h"
#include "../rng.h"
#include "../noise.h"
#include "../math.h"
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
 *
 * rlk does NOT hardcode terrain semantics. The caller provides a mapping
 * (rlk_region_terrain) from normalized elevation/humidity to a tile type and
 * a solidity decision, so each game owns its own palette and rules. The
 * builtin default mapping emulates a basic outdoors map.
 */

/* Normalized sample at a tile. Both in [0, 1]. */
typedef struct {
    float elevation;
    float humidity;
} rlk_region_sample_t;

/*
 * Terrain resolver: the game maps a normalized sample to a tile value and a
 * solidity flag. Returns rlk_tile_t in *out_type and sets *out_solid (1/0).
 * Returning RLK_E_CONFIG aborts generation. NULL => use the builtin default.
 */
typedef rlk_err_t (*rlk_region_terrain)(const rlk_region_sample_t *s,
                                        rlk_tile_t *out_type, int *out_solid);

typedef struct {
    uint64_t             world_seed;
    int32_t              region_x, region_y;   /* region grid coords */
    rlk_region_terrain   terrain;             /* NULL = builtin default */
} rlk_region_cfg_t;

/* Builtin default terrain mapping thresholds (used when terrain == NULL). */
#define RLK_REG_WATER_LEVEL  0.30f
#define RLK_REG_SAND_LEVEL   0.36f
#define RLK_REG_STONE_LEVEL  0.72f

/* Build a per-region noise seed from the world seed and region coordinates. */
static inline uint64_t
rlk_region_seed(const rlk_region_cfg_t *cfg) {
    assert(cfg);
    uint64_t s = cfg->world_seed
               ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(uint32_t)cfg->region_x)
               ^ (0x6A09E667F3BCC909ULL * (uint64_t)(uint32_t)cfg->region_y);
    return s;
}

/* Builtin default terrain resolver. */
static inline rlk_err_t
rlk_region_default_terrain(const rlk_region_sample_t *s,
                           rlk_tile_t *out_type, int *out_solid) {
    assert(s && out_type && out_solid);
    /* Default palette: the game may define matching rlk_tile_t values. */
    enum { DEF_VOID = 0, DEF_WATER = 1, DEF_SAND = 2, DEF_GRASS = 3,
           DEF_DIRT = 4, DEF_STONE = 5 };
    if (s->elevation < RLK_REG_WATER_LEVEL) {
        *out_type = DEF_WATER; *out_solid = 1;
    } else if (s->elevation < RLK_REG_SAND_LEVEL) {
        *out_type = DEF_SAND; *out_solid = 0;
    } else if (s->elevation >= RLK_REG_STONE_LEVEL) {
        *out_type = DEF_STONE; *out_solid = 1;
    } else if (s->humidity > 0.5f) {
        *out_type = DEF_GRASS; *out_solid = 0;
    } else {
        *out_type = DEF_DIRT; *out_solid = 0;
    }
    return RLK_OK;
}

/*
 * Populate `tm` deterministically from `cfg`. `tm` must already be
 * initialized (rlk_tilemap_init). Returns RLK_E_CONFIG if cfg/tm is invalid, or
 * the resolver's error if it returns one. Applies the resolver per tile and
 * sets occ_mask accordingly.
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

    rlk_region_terrain resolve = cfg->terrain
                                 ? cfg->terrain
                                 : rlk_region_default_terrain;

    float ts = tm->tile_size;
    int32_t ox = cfg->region_x * tm->width;
    int32_t oy = cfg->region_y * tm->height;

    for (int32_t y = 0; y < tm->height; y++) {
        for (int32_t x = 0; x < tm->width; x++) {
            float wx = (float)(ox + x) * ts;
            float wy = (float)(oy + y) * ts;
            rlk_region_sample_t s;
            s.elevation = rlk_noise_fbm(&elev, wx, wy, 4, 0.05f, 0.5f);
            s.humidity  = rlk_noise_fbm(&hum,  wx, wy, 3, 0.08f, 0.5f);

            rlk_tilecell_t *c = &tm->tiles[(size_t)y * (size_t)tm->width + (size_t)x];
            rlk_tile_t type = RLK_TILE_VOID;
            int solid = 0;
            rlk_err_t e = resolve(&s, &type, &solid);
            if (e != RLK_OK) return e;
            c->type  = type;
            c->flags = (uint8_t)(solid ? RLK_FLAG_SOLID : 0u);
            c->biome = (uint8_t)(rlk_clampf(s.humidity, 0.0f, 1.0f) * 255.0f);
            rlk_tilemap_set_occ(tm, x, y, solid);
        }
    }
    return RLK_OK;
}

#endif /* RL_REGION_H */
