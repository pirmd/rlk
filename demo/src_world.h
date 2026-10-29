#ifndef SRC_WORLD_H
#define SRC_WORLD_H

/*
 * src_world - viewer source over tilemap_gen (demo component, layer 5).
 *
 * The fantasy-world source of demo_viewer: tilemap_gen map with display
 * modes (tile type, biome, variant, depth + stage probes: coarse elev,
 * final relief, flow accumulation, lake depth) and overlays (river
 * network, POI markers). Extracted from the former demo_tilemap_viewer.
 *
 *   static src_world_t w;
 *   src_world_init(&w, seed);
 *   src_t src = src_world_source(&w);
 *
 * Header-only, POD, caller-owned, no allocation.
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "pxl.h"
#include "source.h"
#include "tilemap.h"
#include "grid.h"
#include "drain.h"
#include "hash.h"
#include "tilemap_gen.h"

/* --- PALETTE ------------------------------------------------------------- */

/* Palette: 4 shades per tile type, light (0) to dark (3).
   Water (sea) is darker than River at the same variant: the sea is deep,
   rivers are shallow. Lakes sit in between. */
static const uint32_t SRC_WORLD_PALETTE[TILE_COUNT][4] = {
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
    { 0xFFD7CCC8U, 0xFFBCAAA4U, 0xFFA1887FU, 0xFF8D6E63U }, /* FLOOR: light -> dark brown */
    { 0xFF80CBC4U, 0xFF4DB6ACU, 0xFF26A69AU, 0xFF00695CU }, /* PINE: light -> dark teal */
    { 0xFFE57373U, 0xFFEF5350U, 0xFFE53935U, 0xFFC62828U }, /* HOUSE: red roof ramp */
    { 0xFFEEEEEEU, 0xFFE0E0E0U, 0xFFBDBDBDU, 0xFF9E9E9EU }, /* PLAZA: pale stone */
    { 0xFFC9A27EU, 0xFFB08968U, 0xFF9C7A5AU, 0xFF7F5539U }, /* DOCK: wood planks */
    { 0xFF4E342EU, 0xFF3E2723U, 0xFF212121U, 0xFF000000U }, /* SINKHOLE: pit, dark rim -> black */
};

static const char *const SRC_WORLD_TILE_NAMES[TILE_COUNT] = {
    "Void", "Grass", "Water", "River", "Lake", "Rock", "Sand", "Tree", "Snow", "Marsh",
    "Floor", "Pine", "House", "Plaza", "Dock", "Sinkhole"
};

static const char *const SRC_WORLD_BIOME_NAMES[Z_COUNT] = {
    "Deep", "Shallow", "Beach", "Desert", "Steppe", "Plains", "Forest",
    "Tundra", "Taiga", "Mountain", "Snow"
};

static const uint32_t SRC_WORLD_POI_COLORS[POI_KIND_COUNT] = {
    0xFF8B4513U, /* CAVE: saddle brown */
    0xFF8B0000U, /* DUNGEON: dark red */
    0xFFFFA500U, /* VILLAGE: orange */
    0xFF9932CCU, /* TOWER: dark orchid */
    0xFFFFD54FU, /* HAMLET: amber */
    0xFF616161U, /* MINE: grey */
    0xFF8D6E63U, /* MILL: brown */
    0xFF0288D1U, /* PORT: blue */
    0xFFFFFF00U, /* LIGHTHOUSE: yellow */
    0xFF546E7AU, /* FORT: blue grey */
};

static const char *const SRC_WORLD_POI_NAMES[POI_KIND_COUNT] = {
    "Cave", "Dungeon", "Village", "Tower", "Hamlet", "Mine",
    "Mill", "Port", "Lighthouse", "Fort"
};

#define SRC_WORLD_BG 0xFF101010U

/* --- MODES / OVERLAYS ---------------------------------------------------- */

/* Base modes: tile fields, then stage probes (wg_nf / drain outputs). */
enum {
    W_MODE_TYPE = 0,   /* tile palette by type+variant */
    W_MODE_BIOME,      /* biome id color */
    W_MODE_VARIANT,    /* variant index, gray ramp */
    W_MODE_DEPTH,      /* tile depth field, blue ramp */
    W_MODE_ELEV,       /* probe: coarse elevation grid (sampled) */
    W_MODE_RELIEF,     /* probe: final per-tile elevation (wg_nf_relief) */
    W_MODE_FLOW,       /* probe: drain flow accumulation */
    W_MODE_LAKE,       /* probe: lake depth */
    W_MODE_COUNT
};

enum {
    W_OVL_RIVER = 0,   /* wg_nf_rivers mask + floodplain carve */
    W_OVL_DETAIL,      /* per-tile detail marks (Type mode only) */
    W_OVL_POI,         /* POI markers */
    W_OVL_COUNT
};

#define W_OVL_RIVER_BIT  (1u << W_OVL_RIVER)
#define W_OVL_DETAIL_BIT (1u << W_OVL_DETAIL)
#define W_OVL_POI_BIT    (1u << W_OVL_POI)

/* --- STATE --------------------------------------------------------------- */

typedef struct {
    tilemap_gen_t gen;
} src_world_t;

static inline void
src_world_init(src_world_t *w, uint64_t seed) {
    assert(w);
    tilemap_gen_generate(&w->gen, seed);
}

/* --- COLOR HELPERS ------------------------------------------------------- */

/* Distinct color per biome id */
static inline uint32_t
src_world_biome_color(uint8_t id) {
    uint32_t h = (uint32_t)id * 0x9E3779B9U + 0x2545F491U;
    uint8_t r = (uint8_t)(h >> 16);
    uint8_t g = (uint8_t)(h >> 8);
    uint8_t b = (uint8_t)h;
    return 0xFF000000U | ((uint32_t)(r & 0xF0) << 16) | ((uint32_t)(g & 0xF0) << 8) | (uint32_t)(b & 0xF0);
}

/* Elevation heatmap: deep blue -> cyan -> green -> brown -> gray -> white */
static inline uint32_t
src_world_elevation_color(float e) {
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
static inline float
src_world_sample_elevation(const tilemap_gen_t *g, int wx, int wy) {
    return rlk_grid_sample(&g->elev_grid, (float)wx + 0.5f, (float)wy + 0.5f);
}

/* --- SOURCE CALLBACKS ---------------------------------------------------- */

static inline uint32_t
src_world_color(void *data, int mode, int wx, int wy) {
    const src_world_t *w = (const src_world_t *)data;
    if (wx < 0 || wx >= TM_W || wy < 0 || wy >= TM_H)
        return SRC_WORLD_BG;

    const rlk_tile_t *tile = rlk_tilemap_get(&w->gen.tm, wx, wy);

    switch (mode) {
    case W_MODE_BIOME:
        return tile ? src_world_biome_color(tile->biome) : SRC_WORLD_BG;
    case W_MODE_VARIANT: {
        uint8_t v = tile ? (tile->variant & 3) : 0;
        uint32_t g = 0x30U + (uint32_t)v * 0x40U;
        return 0xFF000000U | (g << 16) | (g << 8) | g;
    }
    case W_MODE_DEPTH: {
        uint32_t d = tile ? tile->depth : 0;
        return 0xFF000000U | d;   /* blue ramp 0..255 */
    }
    case W_MODE_ELEV:
        return src_world_elevation_color(src_world_sample_elevation(&w->gen, wx, wy));
    case W_MODE_RELIEF:
        return src_world_elevation_color(w->gen.final_elev[wy * TM_W + wx]);
    case W_MODE_FLOW: {
        int ci = 0, cj = 0;
        uint32_t acc = 0;
        if (rlk_grid_cell_of(&w->gen.elev_grid, wx, wy, &ci, &cj))
            acc = w->gen.drain.acc[(size_t)cj * (size_t)w->gen.drain.lattice.w + (size_t)ci];
        float t = (float)acc / ((float)acc + 200.0f);   /* 0..1, saturating */
        uint32_t r = (uint32_t)(t * 0x40f);
        uint32_t g = (uint32_t)(0x30f + t * 0xB0f);
        uint32_t b = (uint32_t)(0xA0f + t * 0x60f);
        return 0xFF000000U | (r << 16) | (g << 8) | b;
    }
    case W_MODE_LAKE: {
        int ci = 0, cj = 0;
        float lk = 0.0f;
        if (rlk_grid_cell_of(&w->gen.elev_grid, wx, wy, &ci, &cj))
            lk = rlk_drain_lake_depth(&w->gen.drain, &w->gen.elev_grid, SEA_LEVEL, ci, cj);
        float t = lk / 0.10f;
        if (t > 1.0f) t = 1.0f;
        uint32_t g = (uint32_t)(0x40f + t * 0xC0f);
        uint32_t b = (uint32_t)(0x20f + t * 0x60f);
        return 0xFF000000U | (g << 8) | b;
    }
    case W_MODE_TYPE:
    default:
        return tile
            ? SRC_WORLD_PALETTE[tile->type % TILE_COUNT][tile->variant & 3]
            : SRC_WORLD_BG;
    }
}

/* River overlay: highlight rivers + floodplains (wg_nf_rivers buffers) */
static inline uint32_t
src_world_overlay(void *data, int o, int wx, int wy, uint32_t base) {
    const src_world_t *w = (const src_world_t *)data;
    if (o != W_OVL_RIVER)
        return base;
    if (wx < 0 || wx >= TM_W || wy < 0 || wy >= TM_H)
        return base;

    const int idx = wy * TM_W + wx;
    const float mask = w->gen.river_mask_buf[idx];
    const float carve = w->gen.river_carve_buf[idx];
    if (mask > 0.0f) {
        /* River: blue gradient */
        uint8_t b = (uint8_t)(0x60 + mask * 0x80);
        uint8_t g = (uint8_t)(0x30 + mask * 0x40);
        return 0xFF000000U | ((uint32_t)b) | ((uint32_t)g << 8);
    }
    if (carve > 0.0f) {
        /* Floodplain: subtle olive tint blended with the base terrain.
           Shows the eroded valley around the river without hiding it. */
        float t = carve / 0.03f;
        if (t > 1.0f) t = 1.0f;
        t *= 0.35f;  /* max 35% blend */
        uint32_t br = (base >> 16) & 0xFF;
        uint32_t bg = (base >> 8) & 0xFF;
        uint32_t bb = base & 0xFF;
        uint32_t r = (uint32_t)((float)br * (1.0f - t) + 0x6Bu * t);
        uint32_t g = (uint32_t)((float)bg * (1.0f - t) + 0x8Eu * t);
        uint32_t b = (uint32_t)((float)bb * (1.0f - t) + 0x23u * t);
        return 0xFF000000U | (r << 16) | (g << 8) | b;
    }
    /* Non-river tiles keep their base color (no dimming) */
    return base;
}

/* Per-tile detail marks drawn over the flat palette (tufts, 2-rect trees,
   shaded rocks, dune lines). Pure function of (seed, tile): deterministic,
   no state. Only meaningful in Type mode; skipped below tile_px < 4. */
static inline void
src_world_draw_details(pxl_canvas_t *cnv, const src_world_t *w, const src_view_t *v) {
    const int t = v->tile_px;
    if (t < 4)
        return;
    for (int sy = 0; sy < v->vh; sy++) {
        for (int sx = 0; sx < v->vw; sx++) {
            int wx = v->wx + sx, wy = v->wy + sy;
            if (wx < 0 || wx >= TM_W || wy < 0 || wy >= TM_H)
                continue;
            const rlk_tile_t *tile = rlk_tilemap_get(&w->gen.tm, wx, wy);
            if (!tile)
                continue;
            const int px = v->dx + sx * t, py = v->dy + sy * t;
            const uint32_t h =
                (uint32_t)rlk_hash_sub(rlk_hash_combine2(w->gen.seed, wx, wy), 0xD37AULL);
            switch (tile->type) {
            case TILE_GRASS: {
                /* tuft: 1px dark blade, height t/3, at hashed x */
                pxl_canvas_set_color(cnv, 0xFF2E7D32U);
                pxl_fill_rect(cnv, px + (int)(h % (uint32_t)t), py + t - t / 3, 1, t / 3);
                break;
            }
            case TILE_TREE: {
                /* broadleaf: brown trunk + dark green canopy (2 rectangles) */
                pxl_canvas_set_color(cnv, 0xFF5D4037U);
                pxl_fill_rect(cnv, px + t / 2 - t / 8, py + t - t / 3, t / 4, t / 3);
                pxl_canvas_set_color(cnv, 0xFF1B5E20U);
                pxl_fill_rect(cnv, px + t / 4, py + t / 8, t / 2, t / 2);
                break;
            }
            case TILE_PINE: {
                /* conifer: trunk + two stacked teal tiers */
                pxl_canvas_set_color(cnv, 0xFF5D4037U);
                pxl_fill_rect(cnv, px + t / 2 - t / 8, py + t - t / 3, t / 4, t / 3);
                pxl_canvas_set_color(cnv, 0xFF00695CU);
                pxl_fill_rect(cnv, px + t / 4, py + t / 8, t / 2, t / 4);
                pxl_fill_rect(cnv, px + t / 3, py + t / 3, t / 3, t / 4);
                break;
            }
            case TILE_ROCK: {
                /* rock block: mid fill, light top/left edge, dark bottom/right */
                const int m = t / 8;
                pxl_canvas_set_color(cnv, 0xFF8D6E63U);
                pxl_fill_rect(cnv, px + m, py + m, t - 2 * m, t - 2 * m);
                pxl_canvas_set_color(cnv, 0xFFD7CCC8U);
                pxl_fill_rect(cnv, px + m, py + m, t - 2 * m, 1);
                pxl_fill_rect(cnv, px + m, py + m, 1, t - 2 * m);
                pxl_canvas_set_color(cnv, 0xFF3E2723U);
                pxl_fill_rect(cnv, px + m, py + t - m - 1, t - 2 * m, 1);
                pxl_fill_rect(cnv, px + t - m - 1, py + m, 1, t - 2 * m);
                break;
            }
            case TILE_SAND: {
                /* dune: 1px horizontal line at hashed height */
                const int dy = py + 1 + (int)((h >> 8) % (uint32_t)(t - 2));
                pxl_canvas_set_color(cnv, 0xFFB8860BU);
                pxl_fill_rect(cnv, px + 1, dy, t - 2, 1);
                break;
            }
            case TILE_HOUSE: {
                /* house: dark red roof over brown walls */
                pxl_canvas_set_color(cnv, 0xFFC62828U);
                pxl_fill_rect(cnv, px + 1, py + 1, t - 2, t / 2);
                pxl_canvas_set_color(cnv, 0xFF6D4C41U);
                pxl_fill_rect(cnv, px + 1, py + 1 + t / 2, t - 2, t - 2 - t / 2);
                break;
            }
            case TILE_DOCK: {
                /* dock: 2 horizontal plank lines over the water */
                pxl_canvas_set_color(cnv, 0xFF7F5539U);
                pxl_fill_rect(cnv, px + 1, py + t / 4, t - 2, 1);
                pxl_fill_rect(cnv, px + 1, py + (3 * t) / 4, t - 2, 1);
                break;
            }
            case TILE_SINKHOLE: {
                /* doline: dark rim, black pit */
                pxl_canvas_set_color(cnv, 0xFF3E2723U);
                pxl_fill_rect(cnv, px + 1, py + 1, t - 2, t - 2);
                pxl_canvas_set_color(cnv, 0xFF000000U);
                pxl_fill_rect(cnv, px + t / 4, py + t / 4, t / 2, t / 2);
                break;
            }
            default:
                break;
            }
        }
    }
}

/* Overlay draw: details (Type mode) or POI markers */
static inline void
src_world_draw(pxl_canvas_t *cnv, void *data, int o, int mode, const src_view_t *v) {
    assert(cnv && data && v);
    const src_world_t *w = (const src_world_t *)data;
    if (o == W_OVL_DETAIL) {
        if (mode == W_MODE_TYPE)
            src_world_draw_details(cnv, w, v);
        return;
    }
    if (o != W_OVL_POI)
        return;

    int half = v->tile_px / 4;
    for (int i = 0; i < w->gen.pois.n; i++) {
        const rlk_poi_t *poi = &w->gen.pois.v[i];
        if (poi->type >= POI_KIND_COUNT) continue;

        int sx = poi->x - v->wx;
        int sy = poi->y - v->wy;
        if (sx < 0 || sx >= v->vw || sy < 0 || sy >= v->vh) continue;

        int cx = v->dx + sx * v->tile_px + v->tile_px / 2;
        int cy = v->dy + sy * v->tile_px + v->tile_px / 2;
        pxl_canvas_set_color(cnv, SRC_WORLD_POI_COLORS[poi->type]);
        pxl_fill_rect(cnv, cx - half, cy - half, half * 2, half * 2);
        pxl_canvas_set_color(cnv, 0xFFFFFFFFU);
        pxl_draw_rect(cnv, cx - half, cy - half, half * 2, half * 2);
    }
}

static inline void
src_world_map_info(char *buf, size_t cap, const void *data) {
    const src_world_t *w = (const src_world_t *)data;
    snprintf(buf, cap,
        "     MAP\n\n"
        "Size : %d x %d\n"
        "Tiles: %d\n"
        "POIs : %d\n"
        "Seed : %llu (R: renew)",
        TM_W, TM_H, TM_W * TM_H,
        w->gen.pois.n,
        (unsigned long long)w->gen.seed);
}

static inline void
src_world_tile_info(char *buf, size_t cap, const void *data, int wx, int wy) {
    const src_world_t *w = (const src_world_t *)data;
    const rlk_tile_t *tile = rlk_tilemap_get(&w->gen.tm, wx, wy);
    if (!tile) {
        snprintf(buf, cap, "     TILE\n\n(%d,%d): out of map", wx, wy);
        return;
    }

    /* Hydrology info from the coarse grid */
    float elev = src_world_sample_elevation(&w->gen, wx, wy);
    int ci = -1, cj = -1;
    uint32_t acc = 0;
    float lake = 0.0f;
    if (rlk_grid_cell_of(&w->gen.elev_grid, wx, wy, &ci, &cj)) {
        acc = w->gen.drain.acc[(size_t)cj * (size_t)w->gen.drain.lattice.w + (size_t)ci];
        lake = rlk_drain_lake_depth(&w->gen.drain, &w->gen.elev_grid, SEA_LEVEL, ci, cj);
    }

    /* Captured derived fields */
    const int idx = wy * TM_W + wx;
    const float fe = w->gen.final_elev[idx];
    const float rm = w->gen.river_mask_buf[idx];
    const float rc = w->gen.river_carve_buf[idx];

    /* Find nearest POI within 16 tiles */
    const rlk_poi_t *near_poi = NULL;
    int near_dist = 9999;
    for (int i = 0; i < w->gen.pois.n; i++) {
        const rlk_poi_t *p = &w->gen.pois.v[i];
        int ddx = p->x - wx;
        int ddy = p->y - wy;
        int d = ddx * ddx + ddy * ddy;
        if (d < near_dist) {
            near_dist = d;
            near_poi = p;
        }
    }

    char poi_line[128] = "";
    if (near_poi && near_dist <= 256 && near_poi->type < POI_KIND_COUNT) {
        int dist = (int)sqrtf((float)near_dist);
        snprintf(poi_line, sizeof(poi_line),
                 "\nPOI: %s d=%d", SRC_WORLD_POI_NAMES[near_poi->type], dist);
    }

    snprintf(buf, cap,
        "     TILE\n\n"
        "Position: (%d,%d)\n"
        "Type    : %s (%d)\n"
        "Biome   : %s (%d)\n"
        "Variant : %d  Flags: 0x%02X\n"
        "Elev    : %.3f  Acc: %u\n"
        "Lake    : %.3f\n"
        "Final   : %.3f\n"
        "River   : %.2f  Carve: %.3f%s",
        wx, wy,
        SRC_WORLD_TILE_NAMES[tile->type % TILE_COUNT], tile->type,
        SRC_WORLD_BIOME_NAMES[tile->biome % Z_COUNT], tile->biome,
        tile->variant, tile->flags,
        elev, acc, lake,
        fe, rm, rc, poi_line);
}

static inline int
src_world_legend(widget_item_t *items, int cap, const void *data,
                 int mode, unsigned overlays) {
    const src_world_t *w = (const src_world_t *)data;
    (void)w;
    int n = 0;

    /* Base mode legend */
    switch (mode) {
    case W_MODE_BIOME:
        for (int i = 0; i < Z_COUNT && n < cap; i++) {
            items[n].color = src_world_biome_color((uint8_t)i);
            items[n].label = SRC_WORLD_BIOME_NAMES[i];
            n++;
        }
        break;
    case W_MODE_VARIANT: {
        static const char *const lbl[4] = { "V0", "V1", "V2", "V3" };
        for (int i = 0; i < 4 && n < cap; i++) {
            uint32_t g = 0x30U + (uint32_t)i * 0x40U;
            items[n].color = 0xFF000000U | (g << 16) | (g << 8) | g;
            items[n].label = lbl[i];
            n++;
        }
        break;
    }
    case W_MODE_DEPTH: {
        static const char *const lbl[4] = { "0", "64", "128", "255" };
        for (int i = 0; i < 4 && n < cap; i++) {
            items[n].color = 0xFF000000U | (uint32_t)(i * 64);
            items[n].label = lbl[i];
            n++;
        }
        break;
    }
    case W_MODE_ELEV:
    case W_MODE_RELIEF: {
        static const widget_item_t stops[6] = {
            { 0xFF000080U, "Deep"  },
            { 0xFF00A0E0U, "Sea"   },
            { 0xFF40A040U, "Low"   },
            { 0xFF8B5A2BU, "High"  },
            { 0xFF808080U, "Peak"  },
            { 0xFFFAFAFAU, "Snow"  },
        };
        for (int i = 0; i < 6 && n < cap; i++)
            items[n++] = stops[i];
        break;
    }
    case W_MODE_FLOW: {
        static const char *const lbl[4] = { "0", "50", "200", "max" };
        for (int i = 0; i < 4 && n < cap; i++) {
            float acc = (i == 3) ? 100000.0f : (float)(i * 50 + (i > 0 ? 25 : 0));
            float t = acc / (acc + 200.0f);
            uint32_t r = (uint32_t)(t * 0x40f);
            uint32_t g = (uint32_t)(0x30f + t * 0xB0f);
            uint32_t b = (uint32_t)(0xA0f + t * 0x60f);
            items[n].color = 0xFF000000U | (r << 16) | (g << 8) | b;
            items[n].label = lbl[i];
            n++;
        }
        break;
    }
    case W_MODE_LAKE: {
        static const char *const lbl[4] = { "0", "0.03", "0.06", "0.10" };
        for (int i = 0; i < 4 && n < cap; i++) {
            float t = (float)i / 3.0f;
            uint32_t g = (uint32_t)(0x40f + t * 0xC0f);
            uint32_t b = (uint32_t)(0x20f + t * 0x60f);
            items[n].color = 0xFF000000U | (g << 8) | b;
            items[n].label = lbl[i];
            n++;
        }
        break;
    }
    case W_MODE_TYPE:
    default:
        /* Tile types (skip VOID) */
        for (int i = 0; i < TILE_COUNT - 1 && n < cap; i++) {
            items[n].color = SRC_WORLD_PALETTE[i + 1][1];
            items[n].label = SRC_WORLD_TILE_NAMES[i + 1];
            n++;
        }
        break;
    }

    /* River overlay legend */
    if ((overlays & W_OVL_RIVER_BIT) && n + 3 <= cap) {
        static const widget_item_t river[3] = {
            { 0xFF303030U, "Land"       },
            { 0xFF6B8E23U, "Floodplain" },
            { 0xFF4060C0U, "River"      },
        };
        for (int i = 0; i < 3; i++)
            items[n++] = river[i];
    }

    /* POI overlay legend */
    if ((overlays & W_OVL_POI_BIT)) {
        for (int i = 0; i < POI_KIND_COUNT && n < cap; i++) {
            items[n].color = SRC_WORLD_POI_COLORS[i];
            items[n].label = SRC_WORLD_POI_NAMES[i];
            n++;
        }
    }

    return n;
}

static inline void
src_world_regen(void *data) {
    src_world_t *w = (src_world_t *)data;
    tilemap_gen_generate(&w->gen, w->gen.seed + 1);
}

/* --- SOURCE DESCRIPTOR --------------------------------------------------- */

static inline src_t
src_world_source(src_world_t *w) {
    assert(w);

    static const char *const mode_names[W_MODE_COUNT] = {
        "Type", "Biome", "Var", "Dpth", "Elev", "Rel", "Flow", "Lake"
    };
    static const char *const overlay_names[W_OVL_COUNT] = {
        "River", "Detail", "POI"
    };

    src_t s = {
        .name = "world",
        .data = w,
        .w = TM_W,
        .h = TM_H,
        .bg = SRC_WORLD_BG,
        .def_mode = W_MODE_TYPE,
        .def_overlays = W_OVL_RIVER_BIT | W_OVL_DETAIL_BIT | W_OVL_POI_BIT,
        .nmodes = W_MODE_COUNT,
        .mode_names = mode_names,
        .color = src_world_color,
        .noverlays = W_OVL_COUNT,
        .overlay_names = overlay_names,
        .overlay = src_world_overlay,
        .draw = src_world_draw,
        .map_info = src_world_map_info,
        .tile_info = src_world_tile_info,
        .legend = src_world_legend,
        .regen = src_world_regen,
    };
    return s;
}

#endif /* SRC_WORLD_H */
