#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pxl.h"
#include "font_6x10.h"

#include "noise.h"
#include "tilemap.h"
#include "grid.h"
#include "wg_flow.h"
#include "wg_nf.h"
#include "wg_nf_grid.h"
#include "wg_nf_flow.h"
#include "wg_nf_rivers.h"
#include "wg_nf_relief.h"
#include "wg_nf_poi.h"

/* Coordinate naming: wx,wy world tile (tilemap coords); x,y view-local;
 * mx,my mouse pixel; px,py screen pixel; dx,dy pixel offset. */

/* --- CONFIGURATION ------------------------------------------------------- */

#define W 1024
#define H 768

#define TILE_PX    16     /* screen pixels per tile */
#define CAM_SPEED  24.0f  /* tiles per second */

#define SIDEBAR_W_RATIO   25  /* sidebar takes 25% of window width */
#define MINIMAP_H_RATIO   34  /* minimap takes 34% of sidebar height */
#define BTN_H             22  /* button height (font 6x10 + padding) */
#define MAP_INFO_H_RATIO  14  /* map info takes 14% of sidebar height */
#define TILE_INFO_H_RATIO 28  /* tile info takes 28% of sidebar height (legend same height, at bottom) */

/* Color (0xAARRGGBB) */
#define FG_COLOR        0xFFFFFFFFU
#define BG_COLOR        0xFF101010U
#define SELECT_COLOR    0xFF00FFFFU
#define HIGHLIGHT_COLOR 0xFFFFFFFFU

/* Demo tile types for visualization (game-defined) */
enum {
    TILE_VOID = 0,
    TILE_GRASS,
    TILE_WATER,
    TILE_RIVER,   /* river: wg_nf_rivers mask > 0.5 */
    TILE_LAKE,    /* lake: filled depression */
    TILE_ROCK,
    TILE_SAND,
    TILE_TREE,
    TILE_SNOW,    /* snow: high elevation or cold temperature */
    TILE_MARSH,   /* marsh: floodplain near river mouths */
    TILE_COUNT
};

/* Palette: 4 shades per tile type, light (0) to dark (3).
   Water (sea) is darker than River at the same variant: the sea is deep,
   rivers are shallow. Lakes sit in between. */
static const uint32_t TILE_PALETTE[TILE_COUNT][4] = {
    { 0xFF000000U, 0xFF000000U, 0xFF000000U, 0xFF000000U }, /* VOID */
    { 0xFFA5D6A7U, 0xFF81C784U, 0xFF66BB6AU, 0xFF4CAF50U }, /* GRASS: light -> green */
    { 0xFF4FC3F7U, 0xFF29B6F6U, 0xFF039BE5U, 0xFF01579BU }, /* WATER (sea): shallow -> abyss */
    { 0xFFB3E5FCU, 0xFF81D4FAU, 0xFF4FC3F7U, 0xFF29B6F6U }, /* RIVER: always lighter than sea */
    { 0xFF3949ABU, 0xFF283593U, 0xFF1A237EU, 0xFF0D1442U }, /* LAKE: indigo, deep but not abyss */
    { 0xFFA1887FU, 0xFF8D6E63U, 0xFF795548U, 0xFF5D4037U }, /* ROCK: light -> dark brown */
    { 0xFFFFE0B2U, 0xFFFFCC80U, 0xFFFFB74DU, 0xFFF9A825U }, /* SAND: pale -> golden */
    { 0xFF43A047U, 0xFF388E3CU, 0xFF2E7D32U, 0xFF1B5E20U }, /* TREE: medium -> dark green */
    { 0xFFFFFFFFU, 0xFFFAFAFAU, 0xFFF5F5F5U, 0xFFEEEEEEU }, /* SNOW: white -> light grey */
    { 0xFF9CCC65U, 0xFF8BC34AU, 0xFF7BA05BU, 0xFF689F38U }, /* MARSH: light -> dark olive */
};

/* Human-readable names for tile types */
static const char *const TILE_NAMES[TILE_COUNT] = {
    "Void", "Grass", "Water", "River", "Lake", "Rock", "Sand", "Tree", "Snow", "Marsh"
};

/* Distinct color per biome id */
static uint32_t
get_biome_color(uint8_t id) {
    uint32_t h = (uint32_t)id * 0x9E3779B9U + 0x2545F491U;
    uint8_t r = (uint8_t)(h >> 16);
    uint8_t g = (uint8_t)(h >> 8);
    uint8_t b = (uint8_t)h;
    return 0xFF000000U | ((unsigned)(r & 0xF0) << 16) |
           ((unsigned)(g & 0xF0) << 8) | (unsigned)(b & 0xF0);
}

/* --- TILEMAP CONFIG ------------------------------------------------------ */

#define TM_W 128
#define TM_H 96

/* Base display modes (mutually exclusive, radio buttons) */
enum {
    BASE_TILE = 0,
    BASE_BIOME,
    BASE_ELEV,
    BASE_RELIEF,
    BASE_COUNT
};

/* Overlay flags (cumulative, toggle buttons) */
#define OVL_RIVER  0x01U  /* show wg_nf_rivers network */
#define OVL_POI    0x02U  /* show POI markers */

/* --- TERRAIN GENERATION (wg_nf + wg_nf_relief + wg_nf_rivers) ------------- */

#define SEED 42ULL

/* Elevation thresholds (relief stage output ~[0,1]).
   Beach is a thin slice (0.03, like demo_worldgen) so coasts read naturally. */
#define ELEV_DEEP     0.33f
#define ELEV_SHALLOW  0.36f
#define ELEV_BEACH    0.39f
#define ELEV_LOWLAND  0.78f
#define ELEV_MOUNTAIN 0.88f

/* Coarse grid for hydrology: 2x the map size so rivers have a real catchment.
   64x48 cells of 4 tiles = 256x192 tiles, centred on the 128x96 map. */
#define GRID_W       64
#define GRID_H       48
#define GRID_CELL    4
#define GRID_ORIGIN_X      (-(GRID_W * GRID_CELL / 2 - TM_W / 2))   /* -64 */
#define GRID_ORIGIN_Y      (-(GRID_H * GRID_CELL / 2 - TM_H / 2))   /* -48 */
#define SEA_LEVEL    0.33f      /* ocean level */
#define LAKE_MIN_DEPTH 0.02f    /* min filled depth to be a lake tile */
#define RIVER_ACC_MIN  40u      /* drainage area (cells) at which a river starts */

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
    F_NSAMPLED
};

/* Derived field slots (stage-written) */
enum {
    D_ELEV = 0,       /* final elevation (wg_nf_relief) -> field[F_NSAMPLED + 0] */
    D_RIVER_MASK,    /* river mask [0,1] (wg_nf_rivers) */
    D_RIVER_CARVE,   /* valley carve depth (wg_nf_rivers) */
    D_ACC,           /* flow accumulation (wg_nf_flow) */
    D_LAKE,          /* lake depth (wg_nf_flow) */
    F_NDERIVED
};

/* Absolute field indices */
#define F_ELEV        (F_NSAMPLED + D_ELEV)
#define F_RIVER_MASK  (F_NSAMPLED + D_RIVER_MASK)
#define F_RIVER_CARVE (F_NSAMPLED + D_RIVER_CARVE)
#define F_ACC         (F_NSAMPLED + D_ACC)
#define F_LAKE        (F_NSAMPLED + D_LAKE)

/* Zone (biome) indices */
enum {
    Z_DEEP = 0,
    Z_SHALLOW,
    Z_BEACH,
    Z_GRASS,
    Z_FOREST,
    Z_MOUNTAIN,
    Z_SNOW,
    Z_COUNT
};

static const char *const BIOME_NAMES[Z_COUNT] = {
    "Deep", "Shallow", "Beach", "Grassland", "Forest", "Mountain", "Snow"
};

/* --- POI (Points of Interest) -------------------------------------------- */

enum {
    POI_CAVE = 0,
    POI_DUNGEON,
    POI_VILLAGE,
    POI_TOWER,
    POI_KIND_COUNT
};

static const uint32_t POI_COLORS[POI_KIND_COUNT] = {
    0xFF8B4513U, /* CAVE: saddle brown */
    0xFF8B0000U, /* DUNGEON: dark red */
    0xFFFFA500U, /* VILLAGE: orange */
    0xFF9932CCU, /* TOWER: dark orchid */
};

static const char *const POI_NAMES[POI_KIND_COUNT] = {
    "Cave", "Dungeon", "Village", "Tower"
};

static int
poi_kind_index(rlk_wg_poi_kind_t kind) {
    switch (kind) {
    case RLK_WG_POI_CAVE:    return POI_CAVE;
    case RLK_WG_POI_DUNGEON: return POI_DUNGEON;
    case RLK_WG_POI_VILLAGE: return POI_VILLAGE;
    case RLK_WG_POI_TOWER:   return POI_TOWER;
    default:                 return -1;
    }
}

/* --- STRUCTS ------------------------------------------------------------- */

typedef struct world_s world_t;

/* Capture wrapper: intercepts zone writer calls to store derived fields
   into per-tile buffers for the Relief and Flow display modes. */
typedef struct {
    world_t *w;
    void (*write)(const rlk_wg_nf_ctx_t *, const void *, rlk_tile_t *);
    const void *real_data;
} capture_t;

/* World/model state */
struct world_s {
    rlk_tilemap_t tm;
    uint8_t buffer[TM_W * TM_H * sizeof(rlk_tile_t)];
    rlk_wg_nf_t wg;

    /* Coarse elevation grid + flow (hydrology) */
    rlk_grid_t elev_grid;
    float elev_buf[GRID_W * GRID_H];
    rlk_wg_flow_t flow;
    uint16_t flow_fill[GRID_W * GRID_H];
    uint8_t  flow_dir[GRID_W * GRID_H];
    uint32_t flow_acc[GRID_W * GRID_H];
    int      flow_order[GRID_W * GRID_H];
    uint64_t flow_scratch[GRID_W * GRID_H];
    rlk_grid_t lake_grid;       /* lake depths, filtered by outlet accumulation */
    float lake_buf[GRID_W * GRID_H];
    rlk_noise_t relief_noise[F_NSAMPLED];

    /* wg_nf stage configs (must outlive wg) */
    rlk_wg_nf_flow_t flow_stage;
    rlk_wg_nf_rivers_t rivers_stage;
    rlk_wg_nf_relief_t relief_stage;

    /* POI detection */
    rlk_wg_poi_list_t pois;
    rlk_wg_nf_poi_t poi_cave_cfg;
    rlk_wg_nf_poi_t poi_dungeon_cfg;
    rlk_wg_nf_poi_t poi_village_cfg;
    rlk_wg_nf_poi_t poi_tower_cfg;

    /* Per-tile captured derived fields (for Relief/Flow display modes) */
    float final_elev[TM_W * TM_H];
    float river_mask_buf[TM_W * TM_H];
    float river_carve_buf[TM_W * TM_H];

    /* Capture wrappers (one per zone) */
    capture_t cap[Z_COUNT];

    uint64_t seed;
};

/* Camera */
typedef struct {
    float wx, wy;
} camera_t;

/* View state */
typedef struct {
    camera_t cam;
    int selected_wx, selected_wy;
    int base_mode;       /* BASE_TILE, BASE_BIOME, BASE_ELEV, BASE_RELIEF */
    uint8_t overlays;    /* OVL_RIVER | OVL_POI */
} view_t;

/* UI resources + per-frame input snapshot */
typedef struct {
    const pxl_font_t **fonts;
    size_t font_count;
    int mx, my;
    bool mouse_left_down;
    bool mouse_left_clicked;
} ui_t;

/* Per-frame layout */
typedef struct {
    pxl_rect_t root;
    pxl_rect_t map;
    pxl_rect_t minimap;
    pxl_rect_t base_mode;   /* top button bar: Tile/Biome/Elev/Relief */
    pxl_rect_t overlay_mode; /* bottom button bar: River/POI */
    pxl_rect_t map_info;
    pxl_rect_t legend;
    pxl_rect_t tile_info;
} layout_t;

/* Minimap view */
typedef struct {
    float scale;
    int off_x, off_y;
    int pix_w, pix_h;
} minimap_t;

/* Viewer state */
typedef struct {
    world_t world;
    view_t view;
    ui_t ui;
    layout_t layout;
    minimap_t mm;
} viewer_t;

/* --- HELPERS ------------------------------------------------------------- */

static bool
pt_in_rect(int x, int y, pxl_rect_t r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* --- TERRAIN WRITERS ----------------------------------------------------- */

/* Widen the river mask near the coast: up to 2x at the shoreline.
   Uses the final elevation to estimate distance from the sea.
   shore = 0 at waterline, 1 at ELEV_BEACH (landward edge of beach). */
static inline float
estuary_mask(const rlk_wg_nf_ctx_t *ctx, float mask) {
    if (mask <= 0.0f) return 0.0f;
    float elev = ctx->field[F_ELEV];
    float shore = (elev - SEA_LEVEL) / (ELEV_BEACH - SEA_LEVEL);
    shore = shore < 0.0f ? 0.0f : (shore > 1.0f ? 1.0f : shore);
    /* Widen: 1x inland, 2x at the shoreline */
    return mask * (1.0f + (1.0f - shore));
}

/* Context for relief_elev_fn: reads field configs from wg, mirrors the wg_nf_relief stage. */
typedef struct {
    const rlk_wg_nf_t *wg;       /* field configs (freq, octaves, gain, kind) */
    const rlk_noise_t *noise;    /* relief_noise[F_NSAMPLED] */
    float warp;
    float hills_amp;
    float mountain_lo, mountain_hi, mountain_amp;
} relief_elev_ctx_t;

/* Sample a wg_nf field at (x,y), reading parameters from wg->fields[idx]. */
static inline float
relief_sample_field(const rlk_wg_nf_t *wg, const rlk_noise_t *noise,
                    int idx, float x, float y) {
    const rlk_wg_nf_field_t *f = &wg->fields[idx];
    float v = rlk_noise_fbm(&noise[idx], x, y, f->octaves, f->freq, f->gain);
    if (f->kind == RLK_WG_NF_RIDGE)
        v = 1.0f - fabsf(2.0f * v - 1.0f);
    return v;
}

/* Elevation grid function: reproduces the wg_nf_relief stage's BASE elevation
   (cont + hills + mountains, before river/fjord carving), so the flow grid
   sees the same terrain that is displayed. Rivers follow the displayed relief.
   Field parameters are read from wg->fields[] to stay in sync with the stage. */
static float
relief_elev_fn(float px, float py, const void *user) {
    const relief_elev_ctx_t *ctx = (const relief_elev_ctx_t *)user;
    const rlk_wg_nf_t *wg = ctx->wg;

    /* Continent at original position */
    float cont = relief_sample_field(wg, ctx->noise, F_CONT, px, py);

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

    return elev;
}

/* Capture writer: stores derived fields then dispatches to the real writer. */
static void
capture_write(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    const capture_t *cap = (const capture_t *)data;
    const int idx = ctx->y * TM_W + ctx->x;
    cap->w->final_elev[idx] = ctx->field[F_ELEV];
    cap->w->river_mask_buf[idx] = ctx->field[F_RIVER_MASK];
    cap->w->river_carve_buf[idx] = ctx->field[F_RIVER_CARVE];
    cap->write(ctx, cap->real_data, out);
}

static void
write_deep(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
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
write_shallow(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
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
write_beach(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
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
    out->type = TILE_SAND;
    /* Wet sand near the waterline (depth=0, darker), dry sand inland (depth=1, lighter) */
    out->variant = (uint8_t)((1.0f - ctx->depth) * 3.0f);
}

static void
write_grass(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    /* Lake: filled depression. At the shore with a river, show a channel. */
    float lake = ctx->field[F_LAKE];
    if (lake >= LAKE_MIN_DEPTH) {
        float mask = estuary_mask(ctx, ctx->field[F_RIVER_MASK]);
        if (mask > 0.3f && lake < LAKE_MIN_DEPTH * 2.0f) {
            out->type = TILE_RIVER;
            out->variant = (uint8_t)(mask * 3.0f);
            out->flags = 0x02;
            return;
        }
        out->type = TILE_LAKE;
        out->variant = 0;
        out->flags = 0x02;
        return;
    }
    /* River (widened near coast by estuary_mask) */
    float mask = estuary_mask(ctx, ctx->field[F_RIVER_MASK]);
    if (mask > 0.5f) {
        out->type = TILE_RIVER;
        out->variant = (uint8_t)(mask * 3.0f);
        out->flags = 0x02;
        return;
    }
    /* Marsh: floodplain near the coast (river edge or valley, low elevation) */
    float carve = ctx->field[F_RIVER_CARVE];
    if ((mask > 0.0f || carve > 0.0f) && ctx->field[F_ELEV] < ELEV_BEACH + 0.08f) {
        out->type = TILE_MARSH;
        out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
        out->flags = 0x02;
        return;
    }
    out->type = TILE_GRASS;
    /* Variant: mix detail noise with depth (darker green in the biome centre) */
    out->variant = (uint8_t)((ctx->field[F_DETAIL] * 0.6f + ctx->depth * 0.4f) * 3.0f);
    /* Rocky outcrops near mountain borders */
    if (ctx->zone2 == Z_MOUNTAIN && ctx->edge > 0.6f &&
        rlk_wg_nf_rand(ctx->wg, ctx->wx, ctx->wy, 2) < 0.3f) {
        out->type = TILE_ROCK;
        out->flags = 0x01;
    }
}

static void
write_forest(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    /* Lake */
    if (ctx->field[F_LAKE] >= LAKE_MIN_DEPTH) {
        out->type = TILE_LAKE;
        out->variant = 0;
        out->flags = 0x02;
        return;
    }
    /* River (widened near coast) */
    float mask = estuary_mask(ctx, ctx->field[F_RIVER_MASK]);
    if (mask > 0.5f) {
        out->type = TILE_RIVER;
        out->variant = (uint8_t)(mask * 3.0f);
        out->flags = 0x02;
        return;
    }
    /* Marsh: floodplain near the coast */
    float carve = ctx->field[F_RIVER_CARVE];
    if ((mask > 0.0f || carve > 0.0f) && ctx->field[F_ELEV] < ELEV_BEACH + 0.08f) {
        out->type = TILE_MARSH;
        out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
        out->flags = 0x02;
        return;
    }
    /* Tree density */
    float density = (0.3f + ctx->depth * 0.5f) * (0.5f + ctx->field[F_DETAIL]);
    /* Variant: mix detail noise with depth (darker in the forest centre) */
    out->variant = (uint8_t)((ctx->field[F_DETAIL] * 0.5f + ctx->depth * 0.5f) * 3.0f);
    if (rlk_wg_nf_rand(ctx->wg, ctx->wx, ctx->wy, 1) < density) {
        out->type = TILE_TREE;
        out->flags = 0x01;
    } else {
        out->type = TILE_GRASS;
    }
}

static void
write_mountain(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    /* Lake */
    if (ctx->field[F_LAKE] >= LAKE_MIN_DEPTH) {
        out->type = TILE_LAKE;
        out->variant = 0;
        out->flags = 0x02;
        return;
    }
    /* River cuts through mountain valleys */
    float mask = estuary_mask(ctx, ctx->field[F_RIVER_MASK]);
    if (mask > 0.5f) {
        out->type = TILE_RIVER;
        out->variant = (uint8_t)(mask * 3.0f);
        out->flags = 0x02;
        return;
    }
    /* Snow only on extreme peaks (like demo_worldgen: snow line at 0.98) */
    if (ctx->field[F_ELEV] > 0.95f) {
        out->type = TILE_SNOW;
        out->flags = 0x01;
        out->variant = 0;  /* pure white */
        return;
    }
    out->type = TILE_ROCK;
    out->flags = 0x01;
    /* Variant: depth-based (darker rock in the mountain core) */
    out->variant = (uint8_t)(ctx->depth * 3.0f);
}

static void
write_snow(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    /* River cuts through snow */
    float mask = estuary_mask(ctx, ctx->field[F_RIVER_MASK]);
    if (mask > 0.5f) {
        out->type = TILE_RIVER;
        out->variant = (uint8_t)(mask * 3.0f);
        out->flags = 0x02;
        return;
    }
    out->type = TILE_SNOW;
    out->flags = 0x01;
    out->variant = 0;  /* pure white */
}

/* --- WORLD GENERATION ---------------------------------------------------- */

static void
world_generate(world_t *w, uint64_t seed) {
    assert(w);
    w->seed = seed;

    rlk_tilemap_init(&w->tm, TM_W, TM_H, w->buffer, sizeof(w->buffer));

    /* --- wg_nf field config (before grid fill: relief_noise seeding needs field seeds) --- */
    rlk_wg_nf_t *wg = &w->wg;
    wg->seed = seed;
    wg->nstages = 0;
    wg->nsampled = F_NSAMPLED;
    wg->nderived = F_NDERIVED;
    wg->fields[F_CONT]   = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 1, .octaves = 3, .freq = 0.012f, .gain = 0.5f };
    wg->fields[F_HILLS]  = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 2, .octaves = 4, .freq = 0.050f, .gain = 0.5f };
    wg->fields[F_RIDGE]  = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_RIDGE, .seed = 3, .octaves = 4, .freq = 0.030f, .gain = 0.5f };
    wg->fields[F_WARP_X] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 4, .octaves = 2, .freq = 0.025f, .gain = 0.5f };
    wg->fields[F_WARP_Y] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 5, .octaves = 2, .freq = 0.025f, .gain = 0.5f };
    wg->fields[F_FJORD]  = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 6, .octaves = 2, .freq = 0.040f, .gain = 0.5f };
    wg->fields[F_TEMP]   = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 7, .octaves = 3, .freq = 0.018f, .gain = 0.5f };
    wg->fields[F_DETAIL] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 8, .octaves = 2, .freq = 0.080f, .gain = 0.5f };
    wg->fields[F_WIG_X]  = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 9, .octaves = 2, .freq = 0.100f, .gain = 0.5f };
    wg->fields[F_WIG_Y]  = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 10, .octaves = 2, .freq = 0.120f, .gain = 0.5f };
    wg->fields[F_CREEK]  = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM,   .seed = 11, .octaves = 2, .freq = 0.025f, .gain = 0.5f };

    /* --- Coarse elevation grid: seed noise like wg_nf, fill with relief terrain ---
       The flow grid must see the SAME terrain the relief stage displays,
       otherwise rivers follow a different landscape than what is shown. */
    for (int i = 0; i < F_NSAMPLED; i++) {
        uint64_t s = rlk_hash_sub(seed, 0xF1E1D000ULL + (uint64_t)i);
        rlk_noise_seed(&w->relief_noise[i], rlk_hash_sub(s, wg->fields[i].seed));
    }
    relief_elev_ctx_t elev_ctx = {
        .wg = wg,
        .noise = w->relief_noise,
        .warp = 8.0f,
        .hills_amp = 0.30f,
        .mountain_lo = 0.60f,
        .mountain_hi = 0.80f,
        .mountain_amp = 0.45f,
    };
    w->elev_grid = (rlk_grid_t){ .lattice = { .w = GRID_W, .h = GRID_H, .cellsize = GRID_CELL,
                                    .origin_x = GRID_ORIGIN_X, .origin_y = GRID_ORIGIN_Y }, .v = w->elev_buf };
    for (int j = 0; j < GRID_H; j++)
        for (int i = 0; i < GRID_W; i++) {
            float cx, cy;
            rlk_grid_center(&w->elev_grid, i, j, &cx, &cy);
            w->elev_buf[(size_t)(j * GRID_W + i)] = relief_elev_fn(cx, cy, &elev_ctx);
        }

    /* --- Flow: depression filling + D8 drainage + accumulation (wg_flow) --- */
    w->flow = (rlk_wg_flow_t){ .w = GRID_W, .h = GRID_H,
                               .fill = w->flow_fill, .dir = w->flow_dir,
                               .acc = w->flow_acc, .order = w->flow_order };
    rlk_err_t ferr = rlk_wg_flow_build(&w->flow, &w->elev_grid, SEA_LEVEL, w->flow_scratch);
    assert(ferr == RLK_OK);

    /* --- Lake grid: filled depression depths, filtered by outlet accumulation ---
       Lakes whose outflow drains fewer than RIVER_ACC_MIN cells are dropped:
       they would sit with no visible outlet. */
    w->lake_grid = (rlk_grid_t){ .lattice = { .w = GRID_W, .h = GRID_H, .cellsize = GRID_CELL,
                                    .origin_x = GRID_ORIGIN_X, .origin_y = GRID_ORIGIN_Y }, .v = w->lake_buf };
    for (int j = 0; j < GRID_H; j++)
        for (int i = 0; i < GRID_W; i++)
            w->lake_buf[(size_t)(j * GRID_W + i)] =
                rlk_wg_flow_lake_depth(&w->flow, &w->elev_grid, SEA_LEVEL, i, j);
    /* Filter: drop lake cells whose outlet accumulation < RIVER_ACC_MIN */
    for (int c = 0; c < GRID_W * GRID_H; c++) {
        if (w->lake_buf[c] <= 0.0f) continue;
        int cur = c;
        for (;;) {
            if (w->flow.dir[cur] == RLK_WG_FLOW_OUTLET) break;
            int dx, dy;
            rlk_wg_flow_step(w->flow.dir[cur], &dx, &dy);
            const int nx = (cur % GRID_W) + dx, ny = (cur / GRID_W) + dy;
            if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) break;
            const int nc = ny * GRID_W + nx;
            if (w->lake_buf[nc] <= 0.0f) break;
            cur = nc;
        }
        if (w->flow.acc[cur] < RIVER_ACC_MIN) w->lake_buf[c] = 0.0f;
    }

    /* --- wg_nf stages: flow + rivers + relief + POI --- */

    /* Stage 1: wg_nf_flow — inject accumulation + lake depth */
    w->flow_stage = (rlk_wg_nf_flow_t){
        .flow = &w->flow,
        .elev = &w->elev_grid,
        .sea = SEA_LEVEL,
        .slot_acc  = D_ACC,
        .slot_dir  = RLK_WG_NF_FLOW_NONE,
        .slot_lake = D_LAKE,
        .acc_scale = 1.0f,
    };
    rlk_wg_nf_flow_add(wg, &w->flow_stage);

    /* Stage 2: wg_nf_rivers — project drainage into per-tile river masks.
       Narrow rivers (max ~4 tiles wide), valleys 2x river width.
       Meanders from F_WIG_X/Y, creeks from F_CREEK, lakes from the filtered grid. */
    w->rivers_stage = (rlk_wg_nf_rivers_t){
        .flow = &w->flow,
        .elev = &w->elev_grid,
        .sea = SEA_LEVEL,
        .acc_min = RIVER_ACC_MIN,
        .width_min = 0.6f,
        .width_max = 2.0f,
        .valley_mult = 2.0f,
        .carve = 0.03f,
        .slot_mask = D_RIVER_MASK,
        .slot_carve = D_RIVER_CARVE,
        .lake_depth = &w->lake_grid,
        .lake_min_depth = LAKE_MIN_DEPTH,
        .f_wig_x = F_WIG_X,
        .f_wig_y = F_WIG_Y,
        .wig_tiles = 3.0f,
        .f_creek = F_CREEK,
        .creek_width = 1.0f,
        .creek_carve = 0.02f,
    };
    rlk_wg_nf_rivers_add(wg, &w->rivers_stage);

    /* Stage 3: wg_nf_relief — standardised elevation (domain warp, fjords, lapse) */
    w->relief_stage = (rlk_wg_nf_relief_t){
        .f_cont = F_CONT,
        .f_hills = F_HILLS,
        .f_ridge = F_RIDGE,
        .f_warp_x = F_WARP_X,
        .f_warp_y = F_WARP_Y,
        .f_fjord = F_FJORD,
        .f_temp = F_TEMP,
        .f_river = RLK_WG_NF_RELIEF_NONE,  /* rivers come from wg_nf_rivers */
        .slot_elev = D_ELEV,
        .slot_river = RLK_WG_NF_RELIEF_NONE,
        .river_carve_field = D_RIVER_CARVE,  /* read carve from wg_nf_rivers */
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
    rlk_wg_nf_relief_add(wg, &w->relief_stage);

    /* Stage 4-7: POI detectors */
    rlk_wg_poi_list_init(&w->pois);

    /* Cave: on hills/mountains, high detail noise */
    w->poi_cave_cfg = (rlk_wg_nf_poi_t){
        .list = &w->pois,
        .kind = RLK_WG_POI_CAVE,
        .salt = 0xCAFEULL,
        .field = F_DETAIL,
        .threshold = 0.70f,
        .density = 0.003f,
        .size = 24,
        .grid = &w->elev_grid,
        .grid_min = 0.40f,
        .grid_max = 0.85f,
        .min_separation = 24,
    };
    rlk_wg_nf_poi_add(wg, &w->poi_cave_cfg);

    /* Dungeon: near rivers */
    w->poi_dungeon_cfg = (rlk_wg_nf_poi_t){
        .list = &w->pois,
        .kind = RLK_WG_POI_DUNGEON,
        .salt = 0xD0D0ULL,
        .field = F_DETAIL,
        .threshold = 0.50f,
        .density = 0.002f,
        .size = 32,
        .flow = &w->flow,
        .flow_grid = &w->elev_grid,
        .min_acc = 30.0f,
        .min_separation = 32,
    };
    rlk_wg_nf_poi_add(wg, &w->poi_dungeon_cfg);

    /* Village: warm lowland plains only (temperature + altitude constraints) */
    w->poi_village_cfg = (rlk_wg_nf_poi_t){
        .list = &w->pois,
        .kind = RLK_WG_POI_VILLAGE,
        .salt = 0xA11CEULL,
        .field = F_TEMP,
        .threshold = 0.45f,
        .density = 0.006f,
        .size = 16,
        .grid = &w->elev_grid,
        .grid_min = 0.35f,
        .grid_max = 0.50f,
        .min_separation = 16,
    };
    rlk_wg_nf_poi_add(wg, &w->poi_village_cfg);

    /* Tower: on hills and peaks */
    w->poi_tower_cfg = (rlk_wg_nf_poi_t){
        .list = &w->pois,
        .kind = RLK_WG_POI_TOWER,
        .salt = 0x70E1ULL,
        .field = F_DETAIL,
        .threshold = 0.60f,
        .density = 0.002f,
        .size = 8,
        .grid = &w->elev_grid,
        .grid_min = 0.70f,
        .grid_max = 1.0f,
        .min_separation = 8,
    };
    rlk_wg_nf_poi_add(wg, &w->poi_tower_cfg);

    /* Axes: elevation (relief output) + temperature (cooled by lapse) */
    wg->naxes = 2;
    wg->axes[0] = (rlk_wg_nf_axis_t){ .field = F_ELEV,  .jitter_field = F_DETAIL, .jitter_amp = 0.03f };
    wg->axes[1] = (rlk_wg_nf_axis_t){ .field = F_TEMP,  .jitter_field = F_DETAIL, .jitter_amp = 0.03f };

    /* Zones: elevation slices on axis 0, temperature splits on axis 1 */
    wg->nzones = Z_COUNT;

    /* Setup capture wrappers: each zone writes through capture_write */
    w->cap[Z_DEEP]     = (capture_t){ .w = w, .write = write_deep,     .real_data = NULL };
    w->cap[Z_SHALLOW]  = (capture_t){ .w = w, .write = write_shallow,  .real_data = NULL };
    w->cap[Z_BEACH]    = (capture_t){ .w = w, .write = write_beach,    .real_data = NULL };
    w->cap[Z_GRASS]    = (capture_t){ .w = w, .write = write_grass,    .real_data = NULL };
    w->cap[Z_FOREST]   = (capture_t){ .w = w, .write = write_forest,   .real_data = NULL };
    w->cap[Z_MOUNTAIN] = (capture_t){ .w = w, .write = write_mountain, .real_data = NULL };
    w->cap[Z_SNOW]     = (capture_t){ .w = w, .write = write_snow,     .real_data = NULL };

    wg->zones[Z_DEEP]     = (rlk_wg_nf_zone_t){ .lo = {0.0f, 0.0f},  .hi = {ELEV_DEEP,     1.0f},  .write = capture_write, .data = &w->cap[Z_DEEP] };
    wg->zones[Z_SHALLOW]  = (rlk_wg_nf_zone_t){ .lo = {ELEV_DEEP, 0.0f},  .hi = {ELEV_SHALLOW,  1.0f},  .write = capture_write, .data = &w->cap[Z_SHALLOW] };
    wg->zones[Z_BEACH]    = (rlk_wg_nf_zone_t){ .lo = {ELEV_SHALLOW, 0.0f}, .hi = {ELEV_BEACH,  1.0f},  .write = capture_write, .data = &w->cap[Z_BEACH] };
    wg->zones[Z_GRASS]    = (rlk_wg_nf_zone_t){ .lo = {ELEV_BEACH, 0.45f}, .hi = {ELEV_LOWLAND, 1.0f},  .write = capture_write, .data = &w->cap[Z_GRASS] };
    wg->zones[Z_FOREST]   = (rlk_wg_nf_zone_t){ .lo = {ELEV_BEACH, 0.0f},  .hi = {ELEV_LOWLAND, 0.45f}, .write = capture_write, .data = &w->cap[Z_FOREST] };
    wg->zones[Z_MOUNTAIN] = (rlk_wg_nf_zone_t){ .lo = {ELEV_LOWLAND, 0.0f}, .hi = {ELEV_MOUNTAIN, 1.0f}, .write = capture_write, .data = &w->cap[Z_MOUNTAIN] };
    wg->zones[Z_SNOW]     = (rlk_wg_nf_zone_t){ .lo = {ELEV_MOUNTAIN, 0.0f}, .hi = {1.0f,        1.0f},  .write = capture_write, .data = &w->cap[Z_SNOW] };

    wg->depth_scale = 0.15f;
    wg->transition_width = 0.10f;

    rlk_wg_nf_init(wg);
    rlk_wg_nf_fill(wg, 0, 0, &w->tm);
}

/* --- LAYOUT -------------------------------------------------------------- */

static layout_t
compute_layout(int win_w, int win_h) {
    layout_t l = {0};
    l.root.w = win_w;
    l.root.h = win_h;

    l.map = l.root;
    pxl_rect_t sidebar = pxl_split_rect(&l.map, l.root.w * SIDEBAR_W_RATIO / 100, PXL_SIDE_RIGHT);

    int leftover_w = l.map.w % TILE_PX;
    if (leftover_w > 0) {
        l.map.w -= leftover_w;
        sidebar.x -= leftover_w;
        sidebar.w += leftover_w;
    }
    int leftover_h = l.map.h % TILE_PX;
    if (leftover_h > 0) {
        l.map.y += leftover_h / 2;
        l.map.h -= leftover_h;
    }

    l.minimap   = pxl_split_rect(&sidebar, sidebar.h * MINIMAP_H_RATIO / 100, PXL_SIDE_TOP);
    l.map_info  = pxl_split_rect(&sidebar, sidebar.h * MAP_INFO_H_RATIO / 100, PXL_SIDE_TOP);
    l.tile_info = pxl_split_rect(&sidebar, sidebar.h * TILE_INFO_H_RATIO / 100, PXL_SIDE_TOP);
    l.legend    = pxl_split_rect(&sidebar, l.tile_info.h, PXL_SIDE_BOTTOM);
    /* Two button bars at the bottom: overlays on bottom, base modes above */
    l.overlay_mode = pxl_split_rect(&sidebar, BTN_H, PXL_SIDE_BOTTOM);
    l.base_mode    = pxl_split_rect(&sidebar, BTN_H, PXL_SIDE_BOTTOM);

    return l;
}

/* --- MINIMAP VIEW -------------------------------------------------------- */

static minimap_t
minimap_view_compute(const pxl_rect_t *area, int map_w, int map_h) {
    assert(area && map_w > 0 && map_h > 0);

    minimap_t mm = {0};
    float scale_x = (float)area->w / (float)map_w;
    float scale_y = (float)area->h / (float)map_h;
    mm.scale = scale_x < scale_y ? scale_x : scale_y;
    mm.pix_w = (int)((float)map_w * mm.scale);
    mm.pix_h = (int)((float)map_h * mm.scale);
    mm.off_x = (area->w - mm.pix_w) / 2;
    mm.off_y = (area->h - mm.pix_h) / 2;
    return mm;
}

static bool
minimap_view_to_tile(const minimap_t *mm, const pxl_rect_t *area,
                     int map_w, int map_h,
                     int mx, int my, int *out_wx, int *out_wy) {
    assert(mm && area && map_w > 0 && map_h > 0 && out_wx && out_wy);

    if (!pt_in_rect(mx, my, *area))
        return false;

    *out_wx = (int)((float)(mx - area->x - mm->off_x) / mm->scale);
    *out_wy = (int)((float)(my - area->y - mm->off_y) / mm->scale);

    if (*out_wx < 0) *out_wx = 0;
    if (*out_wx >= map_w) *out_wx = map_w - 1;
    if (*out_wy < 0) *out_wy = 0;
    if (*out_wy >= map_h) *out_wy = map_h - 1;

    return true;
}

/* --- UI WIDGETS ---------------------------------------------------------- */

static void
ui_draw_text(const ui_t *ui, pxl_canvas_t *cnv, const char *txt, pxl_align_t align) {
    assert(cnv && ui && txt);

    pxl_rect_t bbox = pxl_canvas_view(cnv);

    pxl_writer_t w;
    pxl_writer_init(&w, ui->fonts, ui->font_count);

    pxl_rect_t bounds = pxl_text_bounds(&w, txt);
    pxl_rect_t aligned = pxl_align_rect(bounds, bbox, align);

    pxl_writer_set_cursor(&w, aligned.x, aligned.y);
    pxl_draw_text(cnv, &w, txt);
}

static bool
ui_button(const ui_t *ui, pxl_canvas_t *cnv, const char *label, bool active) {
    ui_draw_text(ui, cnv, label, PXL_ALIGN_H_CENTER | PXL_ALIGN_V_CENTER);

    if (active) {
        pxl_rect_t r = pxl_canvas_view(cnv);
        pxl_canvas_set_color(cnv, SELECT_COLOR);
        pxl_draw_rect(cnv, r.x, r.y, r.w, r.h);
    }

    return ui->mouse_left_clicked && pt_in_rect(ui->mx, ui->my, cnv->scissor);
}

/* --- CAMERA -------------------------------------------------------------- */

static void
camera_center_on(camera_t *cam, int wx, int wy, int view_w, int view_h) {
    assert(cam);
    cam->wx = (float)wx - (float)view_w / (2.0f * (float)TILE_PX);
    cam->wy = (float)wy - (float)view_h / (2.0f * (float)TILE_PX);
}

static void
camera_clamp(camera_t *cam, int map_w, int map_h, int view_w, int view_h) {
    assert(cam);
    float max_x = (float)map_w - (float)view_w / (float)TILE_PX;
    float max_y = (float)map_h - (float)view_h / (float)TILE_PX;
    if (max_x < 0.0f) max_x = 0.0f;
    if (max_y < 0.0f) max_y = 0.0f;
    if (cam->wx < 0.0f) cam->wx = 0.0f; else if (cam->wx > max_x) cam->wx = max_x;
    if (cam->wy < 0.0f) cam->wy = 0.0f; else if (cam->wy > max_y) cam->wy = max_y;
}

/* --- RENDERING ----------------------------------------------------------- */

/* Elevation heatmap: deep blue -> cyan -> green -> brown -> gray -> white */
static uint32_t
get_elevation_color(float e) {
    if (e < SEA_LEVEL) {
        float t = e / SEA_LEVEL;
        uint8_t b = (uint8_t)(0x80 + t * 0x60);
        return 0xFF000000U | ((uint32_t)b);
    }
    e = (e - SEA_LEVEL) / (1.0f - SEA_LEVEL);
    if (e < 0.0f) e = 0.0f;
    if (e > 1.0f) e = 1.0f;

    float r, g, b;
    if (e < 0.3f) {
        float t = e / 0.3f;
        r = 0.2f + t * 0.6f; g = 0.6f + t * 0.3f; b = 0.1f;
    } else if (e < 0.6f) {
        float t = (e - 0.3f) / 0.3f;
        r = 0.8f - t * 0.3f; g = 0.9f - t * 0.4f; b = 0.1f;
    } else if (e < 0.8f) {
        float t = (e - 0.6f) / 0.2f;
        r = 0.5f; g = 0.5f - t * 0.2f; b = 0.1f + t * 0.2f;
    } else {
        float t = (e - 0.8f) / 0.2f;
        r = 0.5f + t * 0.5f; g = 0.3f + t * 0.6f; b = 0.3f + t * 0.7f;
    }
    return 0xFF000000U
        | ((uint32_t)(r * 255.0f) << 16)
        | ((uint32_t)(g * 255.0f) << 8)
        |  (uint32_t)(b * 255.0f);
}

/* Sample elevation at a world tile position from the coarse grid. */
static float
sample_elevation(const world_t *w, int wx, int wy) {
    return rlk_grid_sample(&w->elev_grid, (float)wx + 0.5f, (float)wy + 0.5f);
}

/* Get the base color for a tile according to base_mode. */
static uint32_t
get_base_color(const world_t *w, int base_mode, int twx, int twy) {
    if (base_mode == BASE_ELEV) {
        return get_elevation_color(sample_elevation(w, twx, twy));
    }
    if (base_mode == BASE_RELIEF) {
        if (twx >= 0 && twx < TM_W && twy >= 0 && twy < TM_H)
            return get_elevation_color(w->final_elev[twy * TM_W + twx]);
        return BG_COLOR;
    }
    const rlk_tile_t *tile = rlk_tilemap_get(&w->tm, twx, twy);
    if (!tile) return BG_COLOR;
    return (base_mode == BASE_BIOME)
        ? get_biome_color(tile->biome)
        : TILE_PALETTE[tile->type % TILE_COUNT][tile->variant & 3];
}

static void
render_tilemap(pxl_canvas_t *cnv, const viewer_t *a) {
    assert(cnv && a);

    const world_t *w = &a->world;
    const view_t *v = &a->view;
    const bool ovl_river = (v->overlays & OVL_RIVER) != 0;
    const bool ovl_poi   = (v->overlays & OVL_POI) != 0;

    pxl_rect_t view = pxl_canvas_view(cnv);
    int wx = (int)v->cam.wx;
    int wy = (int)v->cam.wy;
    int dx = -(int)((v->cam.wx - (float)wx) * (float)TILE_PX);
    int dy = -(int)((v->cam.wy - (float)wy) * (float)TILE_PX);
    int vw = view.w / TILE_PX + 2;
    int vh = view.h / TILE_PX + 2;

    for (int y = 0; y < vh; y++) {
        for (int x = 0; x < vw; x++) {
            int twx = wx + x, twy = wy + y;
            int px = dx + x * TILE_PX;
            int py = dy + y * TILE_PX;

            /* Base layer */
            uint32_t color = get_base_color(w, v->base_mode, twx, twy);

            /* River overlay: highlight rivers + floodplains */
            if (ovl_river && twx >= 0 && twx < TM_W && twy >= 0 && twy < TM_H) {
                const int idx = twy * TM_W + twx;
                const float mask = w->river_mask_buf[idx];
                const float carve = w->river_carve_buf[idx];
                if (mask > 0.0f) {
                    /* River: blue gradient */
                    uint8_t b = (uint8_t)(0x60 + mask * 0x80);
                    uint8_t g = (uint8_t)(0x30 + mask * 0x40);
                    color = 0xFF000000U | ((uint32_t)b) | ((uint32_t)g << 8);
                } else if (carve > 0.0f) {
                    /* Floodplain: subtle olive tint blended with the base terrain.
                       Shows the eroded valley around the river without hiding it. */
                    float t = carve / 0.03f;
                    if (t > 1.0f) t = 1.0f;
                    t *= 0.35f;  /* max 35% blend */
                    uint32_t br = (color >> 16) & 0xFF;
                    uint32_t bg = (color >> 8) & 0xFF;
                    uint32_t bb = color & 0xFF;
                    uint32_t r = (uint32_t)((float)br * (1.0f - t) + 0x6Bu * t);
                    uint32_t g = (uint32_t)((float)bg * (1.0f - t) + 0x8Eu * t);
                    uint32_t b = (uint32_t)((float)bb * (1.0f - t) + 0x23u * t);
                    color = 0xFF000000U | (r << 16) | (g << 8) | b;
                }
                /* Non-river tiles keep their base color (no dimming) */
            }

            pxl_canvas_set_color(cnv, color);
            pxl_fill_rect(cnv, px, py, TILE_PX, TILE_PX);
        }
    }

    /* POI markers overlay */
    if (ovl_poi) {
        int half = TILE_PX / 4;
        for (int i = 0; i < w->pois.count; i++) {
            const rlk_wg_poi_t *poi = &w->pois.items[i];
            int ki = poi_kind_index(poi->kind);
            if (ki < 0) continue;

            int sx = poi->wx - wx;
            int sy = poi->wy - wy;
            if (sx < 0 || sx >= vw || sy < 0 || sy >= vh) continue;

            int cx = dx + sx * TILE_PX + TILE_PX / 2;
            int cy = dy + sy * TILE_PX + TILE_PX / 2;
            pxl_canvas_set_color(cnv, POI_COLORS[ki]);
            pxl_fill_rect(cnv, cx - half, cy - half, half * 2, half * 2);
            pxl_canvas_set_color(cnv, HIGHLIGHT_COLOR);
            pxl_draw_rect(cnv, cx - half, cy - half, half * 2, half * 2);
        }
    }

    /* Selected tile outline */
    if (v->selected_wx >= 0 && v->selected_wy >= 0) {
        int x = v->selected_wx - wx;
        int y = v->selected_wy - wy;
        if (x >= 0 && x < vw && y >= 0 && y < vh) {
            pxl_canvas_set_color(cnv, SELECT_COLOR);
            pxl_draw_rect(cnv, dx + x * TILE_PX, dy + y * TILE_PX, TILE_PX, TILE_PX);
        }
    }
}

static void
render_minimap(pxl_canvas_t *cnv, const viewer_t *a) {
    assert(cnv && a);

    const world_t *w = &a->world;
    const view_t *v = &a->view;
    const minimap_t *mm = &a->mm;
    const bool ovl_river = (v->overlays & OVL_RIVER) != 0;
    const bool ovl_poi   = (v->overlays & OVL_POI) != 0;

    pxl_rect_t view = pxl_canvas_view(cnv);
    int mm_w = view.w;
    int mm_h = view.h;

    for (int wy = 0; wy < w->tm.h; wy++) {
        for (int wx = 0; wx < w->tm.w; wx++) {
            int px = mm->off_x + (int)((float)wx * mm->scale);
            int py = mm->off_y + (int)((float)wy * mm->scale);
            int pw = (int)mm->scale + 1;
            int ph = (int)mm->scale + 1;

            if (px + pw > mm_w) pw = mm_w - px;
            if (py + ph > mm_h) ph = mm_h - py;

            uint32_t color = get_base_color(w, v->base_mode, wx, wy);

            if (ovl_river) {
                const int idx = wy * TM_W + wx;
                const float mask = w->river_mask_buf[idx];
                const float carve = w->river_carve_buf[idx];
                if (mask > 0.0f) {
                    uint8_t b = (uint8_t)(0x60 + mask * 0x80);
                    uint8_t g = (uint8_t)(0x30 + mask * 0x40);
                    color = 0xFF000000U | ((uint32_t)b) | ((uint32_t)g << 8);
                } else if (carve > 0.0f) {
                    /* Floodplain: olive tint blended with base terrain */
                    float t = carve / 0.03f;
                    if (t > 1.0f) t = 1.0f;
                    t *= 0.35f;
                    uint32_t br = (color >> 16) & 0xFF;
                    uint32_t bg = (color >> 8) & 0xFF;
                    uint32_t bb = color & 0xFF;
                    uint32_t r = (uint32_t)((float)br * (1.0f - t) + 0x6Bu * t);
                    uint32_t g = (uint32_t)((float)bg * (1.0f - t) + 0x8Eu * t);
                    uint32_t b = (uint32_t)((float)bb * (1.0f - t) + 0x23u * t);
                    color = 0xFF000000U | (r << 16) | (g << 8) | b;
                }
                /* Non-river tiles keep their base color (no dimming) */
            }

            pxl_canvas_set_color(cnv, color);
            pxl_fill_rect(cnv, px, py, pw, ph);
        }
    }

    /* POI markers on minimap */
    if (ovl_poi) {
        int msz = (int)mm->scale + 1;
        if (msz < 2) msz = 2;
        for (int i = 0; i < w->pois.count; i++) {
            const rlk_wg_poi_t *poi = &w->pois.items[i];
            int ki = poi_kind_index(poi->kind);
            if (ki < 0) continue;
            pxl_canvas_set_color(cnv, POI_COLORS[ki]);
            pxl_fill_rect(cnv,
                          mm->off_x + (int)((float)poi->wx * mm->scale),
                          mm->off_y + (int)((float)poi->wy * mm->scale),
                          msz, msz);
        }
    }

    /* Viewport rectangle */
    float visible_w = (float)a->layout.map.w / (float)TILE_PX;
    float visible_h = (float)a->layout.map.h / (float)TILE_PX;

    pxl_canvas_set_color(cnv, HIGHLIGHT_COLOR);
    pxl_draw_rect(cnv,
                  mm->off_x + (int)(v->cam.wx * mm->scale),
                  mm->off_y + (int)(v->cam.wy * mm->scale),
                  (int)(visible_w * mm->scale),
                  (int)(visible_h * mm->scale));

    /* Selected tile indicator */
    if (v->selected_wx >= 0 && v->selected_wy >= 0) {
        pxl_canvas_set_color(cnv, SELECT_COLOR);
        pxl_draw_rect(cnv,
                      mm->off_x + (int)((float)v->selected_wx * mm->scale),
                      mm->off_y + (int)((float)v->selected_wy * mm->scale),
                      (int)mm->scale + 1,
                      (int)mm->scale + 1);
    }
}

/* Base mode buttons (radio: one active at a time) */
static void
render_base_buttons(const ui_t *ui, pxl_canvas_t *cnv_tile, pxl_canvas_t *cnv_biome,
                    pxl_canvas_t *cnv_elev, pxl_canvas_t *cnv_relief, view_t *v) {
    assert(ui && cnv_tile && cnv_biome && cnv_elev && cnv_relief && v);

    if (ui_button(ui, cnv_tile,   "Tile",   v->base_mode == BASE_TILE))
        v->base_mode = BASE_TILE;
    if (ui_button(ui, cnv_biome,  "Biome",  v->base_mode == BASE_BIOME))
        v->base_mode = BASE_BIOME;
    if (ui_button(ui, cnv_elev,   "Elev",   v->base_mode == BASE_ELEV))
        v->base_mode = BASE_ELEV;
    if (ui_button(ui, cnv_relief, "Relief", v->base_mode == BASE_RELIEF))
        v->base_mode = BASE_RELIEF;
}

/* Overlay buttons (toggle: cumulative) */
static void
render_overlay_buttons(const ui_t *ui, pxl_canvas_t *cnv_river, pxl_canvas_t *cnv_poi,
                       view_t *v) {
    assert(ui && cnv_river && cnv_poi && v);

    if (ui_button(ui, cnv_river, "River", (v->overlays & OVL_RIVER) != 0))
        v->overlays ^= OVL_RIVER;
    if (ui_button(ui, cnv_poi,   "POI",   (v->overlays & OVL_POI) != 0))
        v->overlays ^= OVL_POI;
}

static void
render_map_info(pxl_canvas_t *cnv, const ui_t *ui, const viewer_t *a) {
    assert(cnv && ui && a);

    char nfo[255];
    snprintf(nfo, sizeof(nfo),
        "     MAP\n\n"
        "Size : %d x %d\n"
        "Tiles: %d\n"
        "POIs : %d\n"
        "Seed : %llu (R: renew)",
        TM_W, TM_H, TM_W * TM_H,
        a->world.pois.count,
        (unsigned long long)a->world.seed);

    pxl_rect_t r = pxl_canvas_view(cnv);
    pxl_canvas_set_color(cnv, FG_COLOR);
    pxl_draw_rect(cnv, r.x, r.y, r.w, r.h);

    ui_draw_text(ui, cnv, nfo, PXL_ALIGN_V_CENTER | PXL_ALIGN_H_CENTER);
}

static void
render_tile_info(pxl_canvas_t *cnv, const ui_t *ui, const viewer_t *a) {
    assert(cnv && ui && a);

    const view_t *v = &a->view;
    if (v->selected_wx < 0 || v->selected_wy < 0)
        return;

    const world_t *w = &a->world;
    const rlk_tile_t *tile = rlk_tilemap_get(&w->tm, v->selected_wx, v->selected_wy);

    /* Hydrology info from the coarse grid */
    float elev = sample_elevation(w, v->selected_wx, v->selected_wy);
    int ci = -1, cj = -1;
    uint32_t acc = 0;
    float lake = 0.0f;
    if (rlk_grid_cell_of(&w->elev_grid, v->selected_wx, v->selected_wy, &ci, &cj)) {
        acc = w->flow.acc[(size_t)cj * (size_t)w->flow.w + (size_t)ci];
        lake = rlk_wg_flow_lake_depth(&w->flow, &w->elev_grid, SEA_LEVEL, ci, cj);
    }

    /* Captured derived fields */
    const int idx = v->selected_wy * TM_W + v->selected_wx;
    const float fe = w->final_elev[idx];
    const float rm = w->river_mask_buf[idx];
    const float rc = w->river_carve_buf[idx];

    /* Find nearest POI within 16 tiles */
    const rlk_wg_poi_t *near_poi = NULL;
    int near_dist = 9999;
    for (int i = 0; i < w->pois.count; i++) {
        const rlk_wg_poi_t *p = &w->pois.items[i];
        int ddx = p->wx - v->selected_wx;
        int ddy = p->wy - v->selected_wy;
        int d = ddx * ddx + ddy * ddy;
        if (d < near_dist) {
            near_dist = d;
            near_poi = p;
        }
    }

    char poi_line[128] = "";
    if (near_poi && near_dist <= 256) {
        int ki = poi_kind_index(near_poi->kind);
        const char *pname = (ki >= 0) ? POI_NAMES[ki] : "?";
        int dist = (int)sqrtf((float)near_dist);
        char flags_str[32] = "";
        if (near_poi->flags & RLK_WG_POI_NEAR_RIVER) strcat(flags_str, "R");
        if (near_poi->flags & RLK_WG_POI_ON_HILL)    strcat(flags_str, "H");
        if (near_poi->flags & RLK_WG_POI_IN_FOREST)  strcat(flags_str, "F");
        if (near_poi->flags & RLK_WG_POI_COASTAL)    strcat(flags_str, "C");
        if (flags_str[0] == '\0') strcpy(flags_str, "-");
        snprintf(poi_line, sizeof(poi_line),
                 "\nPOI: %s d=%d [%s]", pname, dist, flags_str);
    }

    char nfo[512];
    snprintf(nfo, sizeof(nfo),
        "     TILE\n\n"
        "Position: (%d,%d)\n"
        "Type    : %s (%d)\n"
        "Biome   : %s (%d)\n"
        "Variant : %d  Flags: 0x%02X\n"
        "Elev    : %.3f  Acc: %u\n"
        "Lake    : %.3f\n"
        "Final   : %.3f\n"
        "River   : %.2f  Carve: %.3f%s",
        v->selected_wx, v->selected_wy,
        TILE_NAMES[tile->type % TILE_COUNT], tile->type,
        BIOME_NAMES[tile->biome % Z_COUNT], tile->biome,
        tile->variant, tile->flags,
        elev, acc, lake,
        fe, rm, rc, poi_line);

    pxl_rect_t r = pxl_canvas_view(cnv);
    pxl_canvas_set_color(cnv, FG_COLOR);
    pxl_draw_rect(cnv, r.x, r.y, r.w, r.h);

    ui_draw_text(ui, cnv, nfo, PXL_ALIGN_V_CENTER | PXL_ALIGN_H_CENTER);
}

static void
render_legend(pxl_canvas_t *cnv, const ui_t *ui, const viewer_t *a) {
    assert(cnv && ui && a);

    const view_t *v = &a->view;
    pxl_rect_t view = pxl_canvas_view(cnv);

    pxl_canvas_set_color(cnv, FG_COLOR);
    pxl_draw_rect(cnv, view.x, view.y, view.w, view.h);

    pxl_writer_t w;
    pxl_writer_init(&w, ui->fonts, ui->font_count);

    const int swatch = 8;
    const int line_h = 14;
    const int gap    = 4;
    const int pad    = 4;
    int col_w = view.w / 2;
    int area_y = view.y + 4;
    int area_h = view.h - 8;

    const bool ovl_river = (v->overlays & OVL_RIVER) != 0;
    const bool ovl_poi   = (v->overlays & OVL_POI) != 0;

    /* Count total entries to vertically center */
    int n_base;
    if (v->base_mode == BASE_BIOME)       n_base = Z_COUNT;
    else if (v->base_mode == BASE_ELEV || v->base_mode == BASE_RELIEF) n_base = 6;
    else                                  n_base = TILE_COUNT - 1;  /* Tile: skip VOID */
    int n_river = ovl_river ? 3 : 0;
    int n_poi   = ovl_poi   ? POI_KIND_COUNT : 0;
    int n_total = n_base + n_river + n_poi;
    if (n_total == 0) n_total = 1;

    int y = area_y + (area_h - n_total * line_h) / 2;

    /* --- Base mode legend --- */
    if (v->base_mode == BASE_BIOME) {
        int per_col = (Z_COUNT + 1) / 2;
        for (int i = 0; i < Z_COUNT; i++) {
            int col = i / per_col, row = i % per_col;
            int x = view.x + col * col_w + pad;
            int yy = y + row * line_h;
            pxl_canvas_set_color(cnv, get_biome_color((uint8_t)i));
            pxl_fill_rect(cnv, x, yy + 3, swatch, swatch);
            pxl_writer_set_cursor(&w, x + swatch + gap, yy);
            pxl_draw_text(cnv, &w, BIOME_NAMES[i]);
        }
        y += per_col * line_h;
    } else if (v->base_mode == BASE_ELEV || v->base_mode == BASE_RELIEF) {
        static const struct { float e; uint32_t c; const char *lbl; } stops[] = {
            { 0.00f, 0xFF000080U, "Deep"  },
            { 0.35f, 0xFF00A0E0U, "Sea"   },
            { 0.50f, 0xFF40A040U, "Low"   },
            { 0.70f, 0xFF8B5A2BU, "High"  },
            { 0.85f, 0xFF808080U, "Peak"  },
            { 1.00f, 0xFFFAFAFAU, "Snow"  },
        };
        for (int i = 0; i < 6; i++) {
            pxl_canvas_set_color(cnv, stops[i].c);
            pxl_fill_rect(cnv, view.x + pad, y + 3, swatch, swatch);
            pxl_writer_set_cursor(&w, view.x + pad + swatch + gap, y);
            pxl_draw_text(cnv, &w, stops[i].lbl);
            y += line_h;
        }
    } else {
        /* Tile types (skip VOID): 2 columns */
        int count = TILE_COUNT - 1;
        int per_col = (count + 1) / 2;
        for (int i = 0; i < count; i++) {
            int idx = i + 1;
            int col = i / per_col, row = i % per_col;
            int x = view.x + col * col_w + pad;
            int yy = y + row * line_h;
            pxl_canvas_set_color(cnv, TILE_PALETTE[idx][1]);
            pxl_fill_rect(cnv, x, yy + 3, swatch, swatch);
            pxl_writer_set_cursor(&w, x + swatch + gap, yy);
            pxl_draw_text(cnv, &w, TILE_NAMES[idx]);
        }
        y += per_col * line_h;
    }

    /* --- River overlay legend --- */
    if (ovl_river) {
        static const struct { uint32_t c; const char *lbl; } items[] = {
            { 0xFF303030U, "Land"      },
            { 0xFF6B8E23U, "Floodplain" },
            { 0xFF4060C0U, "River"     },
        };
        for (int i = 0; i < 3; i++) {
            pxl_canvas_set_color(cnv, items[i].c);
            pxl_fill_rect(cnv, view.x + pad, y + 3, swatch, swatch);
            pxl_writer_set_cursor(&w, view.x + pad + swatch + gap, y);
            pxl_draw_text(cnv, &w, items[i].lbl);
            y += line_h;
        }
    }

    /* --- POI overlay legend --- */
    if (ovl_poi) {
        int per_col = (POI_KIND_COUNT + 1) / 2;
        for (int i = 0; i < POI_KIND_COUNT; i++) {
            int col = i / per_col, row = i % per_col;
            int x = view.x + col * col_w + pad;
            int yy = y + row * line_h;
            pxl_canvas_set_color(cnv, POI_COLORS[i]);
            pxl_fill_rect(cnv, x, yy + 3, swatch, swatch);
            pxl_writer_set_cursor(&w, x + swatch + gap, yy);
            pxl_draw_text(cnv, &w, POI_NAMES[i]);
        }
    }
}

/* --- INPUT --------------------------------------------------------------- */

static void
handle_input(pxl_app_t *pxl, viewer_t *a) {
    assert(pxl && a);

    a->ui.mx = pxl->curr.mouse_x;
    a->ui.my = pxl->curr.mouse_y;
    a->ui.mouse_left_down = pxl_app_is_active(pxl, PXL_MOUSE_LEFT);
    a->ui.mouse_left_clicked = pxl_app_was_triggered(pxl, PXL_MOUSE_LEFT);

    /* Camera movement: arrows + hjkl */
    if (pxl_app_is_active(pxl, PXL_KEYB_LEFT)  || pxl_app_is_active(pxl, PXL_KEYB_H)) a->view.cam.wx -= CAM_SPEED;
    if (pxl_app_is_active(pxl, PXL_KEYB_RIGHT) || pxl_app_is_active(pxl, PXL_KEYB_L)) a->view.cam.wx += CAM_SPEED;
    if (pxl_app_is_active(pxl, PXL_KEYB_UP)    || pxl_app_is_active(pxl, PXL_KEYB_K)) a->view.cam.wy -= CAM_SPEED;
    if (pxl_app_is_active(pxl, PXL_KEYB_DOWN)  || pxl_app_is_active(pxl, PXL_KEYB_J)) a->view.cam.wy += CAM_SPEED;

    /* Regenerate terrain with next seed */
    if (pxl_app_was_triggered(pxl, PXL_KEYB_R)) {
        world_generate(&a->world, a->world.seed + 1);
        a->view.selected_wx = -1;
        a->view.selected_wy = -1;
    }

    int mx = a->ui.mx;
    int my = a->ui.my;

    /* Minimap drag: hold left click to pan camera */
    if (a->ui.mouse_left_down) {
        int wx, wy;
        if (minimap_view_to_tile(&a->mm, &a->layout.minimap,
                                 a->world.tm.w, a->world.tm.h,
                                 mx, my, &wx, &wy))
            camera_center_on(&a->view.cam, wx, wy, a->layout.map.w, a->layout.map.h);
    }

    /* Click: minimap center or tile selection */
    if (a->ui.mouse_left_clicked) {
        int wx, wy;
        if (minimap_view_to_tile(&a->mm, &a->layout.minimap,
                                 a->world.tm.w, a->world.tm.h,
                                 mx, my, &wx, &wy)) {
            camera_center_on(&a->view.cam, wx, wy, a->layout.map.w, a->layout.map.h);
        } else if (pt_in_rect(mx, my, a->layout.map)) {
            int twx = (int)(a->view.cam.wx + (float)(mx - a->layout.map.x) / (float)TILE_PX);
            int twy = (int)(a->view.cam.wy + (float)(my - a->layout.map.y) / (float)TILE_PX);

            if (twx >= 0 && twx < a->world.tm.w && twy >= 0 && twy < a->world.tm.h) {
                a->view.selected_wx = twx;
                a->view.selected_wy = twy;
            }
        }
    }

    camera_clamp(&a->view.cam, a->world.tm.w, a->world.tm.h,
                 a->layout.map.w, a->layout.map.h);
}

/* --- RENDER FRAME -------------------------------------------------------- */

static void
render_frame(pxl_buf_t *pb, viewer_t *a) {
    assert(pb && a);

    pxl_canvas_t cnv;
    pxl_canvas_init(&cnv, pb);
    pxl_canvas_set_color(&cnv, BG_COLOR);
    pxl_canvas_clear(&cnv);

    /* Frames around button bars */
    pxl_canvas_set_color(&cnv, FG_COLOR);
    pxl_draw_rect(&cnv, a->layout.base_mode.x, a->layout.base_mode.y,
                  a->layout.base_mode.w, a->layout.base_mode.h);
    pxl_draw_rect(&cnv, a->layout.overlay_mode.x, a->layout.overlay_mode.y,
                  a->layout.overlay_mode.w, a->layout.overlay_mode.h);

    /* Split base_mode bar into 4 equal columns */
    pxl_rect_t bm = a->layout.base_mode;
    int bm_w = bm.w / 4;
    pxl_rect_t bm_tile   = { bm.x,                bm.y, bm_w,              bm.h };
    pxl_rect_t bm_biome  = { bm.x + bm_w,         bm.y, bm_w,              bm.h };
    pxl_rect_t bm_elev   = { bm.x + bm_w * 2,     bm.y, bm_w,              bm.h };
    pxl_rect_t bm_relief = { bm.x + bm_w * 3,     bm.y, bm.w - bm_w * 3,   bm.h };

    /* Split overlay_mode bar into 2 equal columns */
    pxl_rect_t om = a->layout.overlay_mode;
    int om_w = om.w / 2;
    pxl_rect_t om_river = { om.x,            om.y, om_w,            om.h };
    pxl_rect_t om_poi   = { om.x + om_w,      om.y, om.w - om_w,     om.h };

    pxl_canvas_t cnv_map, cnv_minimap, cnv_bm_tile, cnv_bm_biome, cnv_bm_elev, cnv_bm_relief,
                 cnv_om_river, cnv_om_poi, cnv_map_info, cnv_legend, cnv_tile_info;
    pxl_canvas_init_view(&cnv_map, pb, a->layout.map);
    pxl_canvas_init_view(&cnv_minimap, pb, a->layout.minimap);
    pxl_canvas_init_view(&cnv_bm_tile, pb, bm_tile);
    pxl_canvas_init_view(&cnv_bm_biome, pb, bm_biome);
    pxl_canvas_init_view(&cnv_bm_elev, pb, bm_elev);
    pxl_canvas_init_view(&cnv_bm_relief, pb, bm_relief);
    pxl_canvas_init_view(&cnv_om_river, pb, om_river);
    pxl_canvas_init_view(&cnv_om_poi, pb, om_poi);
    pxl_canvas_init_view(&cnv_map_info, pb, a->layout.map_info);
    pxl_canvas_init_view(&cnv_legend, pb, a->layout.legend);
    pxl_canvas_init_view(&cnv_tile_info, pb, a->layout.tile_info);

    render_tilemap(&cnv_map, a);
    render_minimap(&cnv_minimap, a);
    render_base_buttons(&a->ui, &cnv_bm_tile, &cnv_bm_biome, &cnv_bm_elev, &cnv_bm_relief, &a->view);
    render_overlay_buttons(&a->ui, &cnv_om_river, &cnv_om_poi, &a->view);
    render_map_info(&cnv_map_info, &a->ui, a);
    render_legend(&cnv_legend, &a->ui, a);
    render_tile_info(&cnv_tile_info, &a->ui, a);
}

/* --- MAIN ---------------------------------------------------------------- */

int
main(void) {
    pxl_app_cfg_t cfg = {
        .title = "RLK Tilemap Viewer",
        .width = W,
        .height = H,
        .backend_flags = PXL_BACKEND_RESIZABLE,
    };

    pxl_app_t app;
    if (pxl_app_init(&app, &cfg) != PXL_SUCCESS) return 1;

    static viewer_t a;
    a.ui.fonts = (const pxl_font_t *[]){ &font_6x10 };
    a.ui.font_count = 1;
    world_generate(&a.world, SEED);
    a.view.cam.wx = 0.0f;
    a.view.cam.wy = 0.0f;
    a.view.selected_wx = -1;
    a.view.selected_wy = -1;
    a.view.base_mode = BASE_TILE;
    a.view.overlays = OVL_RIVER | OVL_POI;  /* show rivers + POI by default */

    while (pxl_app_advance_wait(&app)) {
        if (pxl_app_was_triggered(&app, PXL_KEYB_ESCAPE))
            break;

        int win_w, win_h;
        pxl_backend_get_window_size(&win_w, &win_h);
        a.layout = compute_layout(win_w, win_h);
        a.mm = minimap_view_compute(&a.layout.minimap, TM_W, TM_H);

        handle_input(&app, &a);

        pxl_buf_t pb;
        if (pxl_backend_begin_frame(&pb) == PXL_SUCCESS) {
            render_frame(&pb, &a);
            (void)pxl_backend_end_frame();
        }
    }

    pxl_app_deinit(&app);
    return 0;
}
