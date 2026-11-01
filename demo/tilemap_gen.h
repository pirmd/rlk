#ifndef TILEMAP_GEN_H
#define TILEMAP_GEN_H

/*
 * tilemap_gen - reusable fantasy map generator (demo component).
 *
 * Composes the whole library into one call: noise fields -> coarse
 * elevation grid -> drain (Priority-Flood + D8 + accumulation) -> wg
 * pipeline (drain, rivers, relief stages + POI detection) -> tilemap.
 *
 *   static tilemap_gen_t gen;            <- ~340 KB: static or global
 *   tilemap_gen_generate(&gen, seed);
 *
 *   gen.tm          filled tilemap (TM_W x TM_H)
 *   gen.pois        detected POIs
 *   gen.final_elev, gen.river_mask_buf, gen.river_carve_buf
 *                   per-tile derived fields, for debug display modes
 *   gen.elev_grid, gen.drain, gen.lake_grid, gen.hydro
 *                   coarse hydrology data, for tooltips and overlays
 *
 * POD, caller-owned, no allocation. Regenerate in place with another seed.
 */

#include <stdint.h>

#include "tilemap.h"
#include "grid.h"
#include "drain.h"
#include "hydro.h"
#include "poi.h"
#include "noise.h"
#include "wg.h"
#include "wg_drain.h"
#include "wg_hydro.h"
#include "wg_region.h"
#include "wg_rivers.h"
#include "wg_relief.h"
#include "wg_poi.h"

/* --- Game-defined tile flag bits (rlk reserves none) --------------------- */

enum {
    DEMO_F_WALKABLE    = 1 << 0,
    DEMO_F_TRANSPARENT = 1 << 1,
};

/* Ready-made writer for the ca/bsp `write` callback: wall/floor by biome
   (0 = wall, > 0 = floor), deterministic variant from (seed, x, y). */
typedef struct { uint8_t wall_type, floor_type; } demo_wallfloor_t;

static inline void
demo_write_wallfloor(uint64_t seed, const void *data, int x, int y,
                     rlk_tile_t *out) {
    const demo_wallfloor_t *wf = (const demo_wallfloor_t *)data;
    out->type = (out->biome == 0) ? wf->wall_type : wf->floor_type;
    out->variant = (uint8_t)(rlk_hash_normf(rlk_hash_at(seed, x, y, 0xCAFEULL)) * 255.0f);
}

/* --- Map geometry -------------------------------------------------------- */

#define TM_W 128
#define TM_H 96

/* Coarse grid for hydrology: 2x the map size so rivers have a real
   catchment. 64x48 cells of 4 tiles = 256x192 tiles, centred on the map. */
#define GRID_W        64
#define GRID_H        48
#define GRID_CELL     4
#define GRID_ORIGIN_X (-(GRID_W * GRID_CELL / 2 - TM_W / 2))   /* -64 */
#define GRID_ORIGIN_Y (-(GRID_H * GRID_CELL / 2 - TM_H / 2))   /* -48 */
#define SEA_LEVEL     0.33f   /* ocean level */

/* --- Tile types (game-defined) ------------------------------------------- */

enum {
    TILE_VOID = 0,
    TILE_GRASS,
    TILE_WATER,
    TILE_RIVER,   /* river: wg_rivers mask > 0.5 */
    TILE_LAKE,    /* lake: filled depression */
    TILE_ROCK,
    TILE_SAND,
    TILE_TREE,
    TILE_SNOW,    /* snow: high elevation or cold temperature */
    TILE_MARSH,   /* marsh: floodplain near river mouths */
    TILE_FLOOR,   /* forest floor: inside Z_FOREST where no tree grew */
    TILE_PINE,    /* conifer: cold forest (low F_TEMP) */
    TILE_HOUSE,   /* building: stamped by the POI content pass (village, fort...) */
    TILE_PLAZA,   /* village square */
    TILE_DOCK,    /* quay: port POI, over water */
    TILE_SINKHOLE, /* karst doline: limestone lowland depression */
    TILE_COUNT
};

/* --- wg field slots --------------------------------------------------- */

/* Sampled field slots (noise fields) */
enum {
    F_CONT = 0,   /* continents (low freq) */
    F_HILLS,      /* rolling relief */
    F_RIDGE,      /* mountain crests (RIDGE kind) */
    F_WARP_X,     /* domain warp X */
    F_WARP_Y,     /* domain warp Y */
    F_FJORD,      /* fjord noise field */
    F_TEMP,       /* temperature (cooled by lapse) */
    F_DETAIL,     /* local detail */
    F_WIG_X,      /* river meander X (fine displacement) */
    F_WIG_Y,      /* river meander Y */
    F_CREEK,      /* creek noise field (level-line headwater streams) */
    F_HUM,        /* humidity (climate axis) */
    F_NSAMPLED
};

/* Derived field slots (stage-written) */
enum {
    D_ELEV = 0,       /* final elevation (wg_relief) */
    D_RIVER_MASK,    /* river mask [0,1] (wg_rivers) */
    D_RIVER_CARVE,   /* valley carve depth (wg_rivers) */
    D_LAKE,          /* lake depth (wg_drain) */
    D_SLOPE,         /* D8 downslope, elev units/tile (wg_hydro) */
    D_STRAHLER,      /* Strahler stream order (wg_hydro) */
    D_SPEED,         /* Manning speed estimate (wg_hydro) */
    D_TEMP_LAT,      /* latitudinal thermal gradient (demo stage, specialist review 2026-10-30) */
    F_NDERIVED
};

/* Absolute field indices */
#define F_ELEV        (F_NSAMPLED + D_ELEV)
#define F_RIVER_MASK  (F_NSAMPLED + D_RIVER_MASK)
#define F_RIVER_CARVE (F_NSAMPLED + D_RIVER_CARVE)
#define F_LAKE        (F_NSAMPLED + D_LAKE)
#define F_SLOPE       (F_NSAMPLED + D_SLOPE)
#define F_STRAHLER    (F_NSAMPLED + D_STRAHLER)
#define F_SPEED       (F_NSAMPLED + D_SPEED)
#define F_TEMP_LAT    (F_NSAMPLED + D_TEMP_LAT)

/* --- Zone (biome) indices ------------------------------------------------ */

/* Zones: water by elevation, then 6 land biomes on the (temp, hum) plane
   (Köppen simplified; humidity axis + 6 biomes from specialist review
   2026-10-30, geographer P0), then mountain/snow by elevation. */
enum {
    Z_DEEP = 0,
    Z_SHALLOW,
    Z_BEACH,
    Z_DESERT,   /* hot + dry   */
    Z_STEPPE,   /* temperate + dry */
    Z_PLAINS,   /* temperate + wet */
    Z_FOREST,   /* hot + wet   */
    Z_TUNDRA,   /* cold + dry  */
    Z_TAIGA,    /* cold + wet  */
    Z_MOUNTAIN,
    Z_SNOW,
    Z_COUNT
};

/* --- Light geology ------------------------------------------------------- */

/* Structure types (per-tile, written by the geology stage) */
enum {
    STRUCT_NONE = 0,
    STRUCT_FAULT,    /* fault line: horst/graben escarpment */
    STRUCT_FOLD,     /* sedimentary fold (anticline/syncline bands) */
    STRUCT_DOME,     /* broad circular uplift */
    STRUCT_VOLCANO,  /* volcanic cone (with crater) */
    STRUCT_COUNT
};

/* Lithology classes (per-tile, colors the rock tiles) */
enum {
    LITHO_SEDIMENT = 0,  /* sedimentary: plains, basins */
    LITHO_META,          /* metamorphic: fault zones, high relief */
    LITHO_INTRUSIVE,     /* granite: dome cores */
    LITHO_EFFUSIVE,      /* basalt: volcanic areas */
    LITHO_COUNT
};

/* --- POI kinds ----------------------------------------------------------- */

enum {
    POI_CAVE = 0,
    POI_DUNGEON,
    POI_VILLAGE,
    POI_TOWER,
    POI_HAMLET,      /* small settlement: plains/steppe */
    POI_MINE,        /* mine: mountains on fault/dome structures */
    POI_MILL,        /* watermill: river reach with some slope */
    POI_PORT,        /* port: beach at a river mouth (estuary) */
    POI_LIGHTHOUSE,  /* lighthouse: cliff coast (steep beach) */
    POI_FORT,        /* fort: hilltop at the mountain foot */
    POI_KIND_COUNT
};

/* --- State --------------------------------------------------------------- */

/* Latitudinal thermal gradient stage config (specialist review 2026-10-30,
   geographer): D_TEMP_LAT = clamp(F_TEMP - temp_drop * |py| / lat_half_span).
   The Köppen classify axis reads the result instead of raw F_TEMP, so map
   edges (poles) are colder. Must run after the relief stage (lapse write-back). */
typedef struct {
    uint8_t f_temp;          /* sampled field read (post-lapse) */
    uint8_t slot_out;        /* derived slot written */
    float   temp_drop;       /* total drop at |py| = lat_half_span (e.g. 0.15) */
    float   lat_half_span;   /* half map height in tiles (TM_H / 2) */
} gen_temp_lat_cfg_t;

typedef struct tilemap_gen_s tilemap_gen_t;

/* Capture wrapper: intercepts zone writer calls to store derived fields
   into per-tile buffers (final_elev, river_mask_buf, river_carve_buf). */
typedef struct {
    tilemap_gen_t *gen;
    void (*write)(const rlk_wg_ctx_t *, const void *, rlk_tile_t *);
    const void *real_data;
} tilemap_gen_capture_t;

/* Generator state: ~300 KB, declare static or global. All buffers are
   caller-owned members; tilemap_gen_generate fills them in place. */
struct tilemap_gen_s {
    rlk_tilemap_t tm;
    uint8_t buffer[TM_W * TM_H * sizeof(rlk_tile_t)];
    rlk_wg_t wg;

    /* Coarse elevation grid + drain (hydrology) */
    rlk_grid_t elev_grid;
    float elev_buf[GRID_W * GRID_H];
    rlk_drain_t drain;
    uint16_t drain_filled[GRID_W * GRID_H];
    uint8_t  drain_dir[GRID_W * GRID_H];
    uint32_t drain_acc[GRID_W * GRID_H];
    int      drain_order[GRID_W * GRID_H];
    uint64_t drain_scratch[GRID_W * GRID_H];
    rlk_grid_t lake_grid;       /* lake depths, filtered by outlet accumulation */
    float lake_buf[GRID_W * GRID_H];
    rlk_hydro_t hydro;          /* derived hydrology on the drain grid */
    float hydro_slope[GRID_W * GRID_H];
    uint8_t hydro_strahler[GRID_W * GRID_H];
    float hydro_speed[GRID_W * GRID_H];
    uint16_t hydro_scratch[GRID_W * GRID_H];
    rlk_noise_t relief_noise[F_NSAMPLED];
    rlk_noise_t geo_noise[3];   /* light geology: faults / folds / domes */

    /* wg stage configs (must outlive wg) */
    rlk_wg_region_cfg_t prov_cfg;      /* provinces: per-region bias on sampled fields */
    rlk_wg_stage_t prov_mod;           /* stock region stage built from prov_cfg */
    rlk_wg_stage_t relief_mod;         /* stock relief stage, wrapped with rain shadow */
    rlk_wg_drain_cfg_t drain_stage;
    rlk_wg_hydro_cfg_t hydro_stage;
    rlk_wg_rivers_cfg_t rivers_stage;
    rlk_wg_relief_cfg_t relief_stage;
    gen_temp_lat_cfg_t temp_lat_cfg;

    /* POI detection */
    rlk_poi_t poi_buf[256];
    rlk_poi_list_t pois;
    rlk_wg_poi_cfg_t poi_cave_cfg;
    rlk_wg_poi_cfg_t poi_dungeon_cfg;
    rlk_wg_poi_cfg_t poi_village_cfg;
    rlk_wg_poi_cfg_t poi_tower_cfg;

    /* Per-tile captured derived fields (for Relief/Flow display modes) */
    float final_elev[TM_W * TM_H];
    float river_mask_buf[TM_W * TM_H];
    float river_carve_buf[TM_W * TM_H];

    /* Per-tile geology (geology stage, after relief) */
    uint8_t struct_buf[TM_W * TM_H];   /* STRUCT_* */
    uint8_t litho_buf[TM_W * TM_H];    /* LITHO_* */

    /* POI content pass: per-tile reservation, 0xFF = free (first POI whose
       bbox covers a tile wins, per the poi.h overlap rule) */
    uint8_t poi_res[TM_W * TM_H];

    /* Capture wrappers (one per zone) */
    tilemap_gen_capture_t cap[Z_COUNT];

    uint64_t seed;
};

/* Generate (or regenerate) the map in place with the given seed. */
void tilemap_gen_generate(tilemap_gen_t *gen, uint64_t seed);

/* --- Post-generation stats (map probes) ---------------------------------- */

typedef struct {
    uint32_t tile_count[TILE_COUNT];  /* histogram of final tile types */
    float entropy;                    /* Shannon entropy of tile types, bits */
    float water_frac;                 /* WATER + LAKE fraction */
    float river_frac;                 /* RIVER fraction */
    float land_frac;                  /* walkable land fraction */
    int n_components;                 /* connected walkable components (4-conn) */
    int largest_component;            /* size in tiles of the largest one */
    int n_sinkholes;                  /* karst dolines */
    int n_poi[POI_KIND_COUNT];        /* POIs per kind */
} tilemap_gen_stats_t;

/* Fill st from gen. scratch = TM_W*TM_H caller-owned bytes (flood fill). */
void tilemap_gen_stats(const tilemap_gen_t *gen, tilemap_gen_stats_t *st,
                       uint8_t *scratch);

/* Print a human-readable summary of st (and the seed) to stdout. */
void tilemap_gen_stats_print(const tilemap_gen_t *gen,
                             const tilemap_gen_stats_t *st);

#endif /* TILEMAP_GEN_H */
