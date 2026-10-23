#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pxl.h"
#include "font_6x10.h"

#include "tilemap.h"
#include "wg_nf.h"

/* Coordinate naming: wx,wy world tile (tilemap coords); x,y view-local;
 * mx,my mouse pixel; px,py screen pixel; dx,dy pixel offset. */

/* --- CONFIGURATION ------------------------------------------------------- */

#define W 1024
#define H 768

#define TILE_PX    16     /* screen pixels per tile */
#define CAM_SPEED  24.0f  /* tiles per second */

#define SIDEBAR_W_RATIO   25  /* sidebar takes 25% of window width */
#define MINIMAP_H_RATIO   37  /* minimap takes 37% of sidebar height */
#define VIEW_MODE_BTN_H   24  /* view mode button height (font 6x10 + padding) */
#define MAP_INFO_H_RATIO  16  /* map info takes 16% of sidebar height */
#define TILE_INFO_H_RATIO 26  /* tile info takes 26% of sidebar height (legend same height, at bottom) */

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
    TILE_ROCK,
    TILE_SAND,
    TILE_TREE,
    TILE_COUNT
};

/* Palette for tile types */
static const uint32_t TILE_COLORS[TILE_COUNT] = {
    0xFF000000U, /* VOID */
    0xFF4CAF50U, /* GRASS */
    0xFF2196F3U, /* WATER */
    0xFF795548U, /* ROCK */
    0xFFFFC107U, /* SAND */
    0xFF2E7D32U, /* TREE */
};

/* Distinct color per biome id: multiply by an odd constant so consecutive
   ids spread over the three channels instead of staying on the gray axis. */
static uint32_t
get_biome_color(uint8_t id) {
    uint32_t h = (uint32_t)id * 0x9E3779B9U + 0x2545F491U;
    uint8_t r = (uint8_t)(h >> 16);
    uint8_t g = (uint8_t)(h >> 8);
    uint8_t b = (uint8_t)h;
    return 0xFF000000U | ((r & 0xF0) << 16) | ((g & 0xF0) << 8) | (b & 0xF0);
}

/* --- TILEMAP CONFIG ------------------------------------------------------ */

#define TM_W 128
#define TM_H 96

#define DISPLAY_TILE  0
#define DISPLAY_BIOME 1

/* --- TERRAIN GENERATION (wg_nf) ------------------------------------------ */

#define SEED 42ULL

/* Elevation thresholds (noise output is ~[0,1]) */
#define ELEV_DEEP     0.35f
#define ELEV_SHALLOW  0.42f
#define ELEV_BEACH    0.46f
#define ELEV_LOWLAND  0.72f
#define ELEV_MOUNTAIN 0.88f

/* Field slots */
enum { F_ELEV = 0, F_TEMP, F_DETAIL, F_NFIELDS };

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

/* Human-readable names for tile types and biomes */
static const char *const TILE_NAMES[TILE_COUNT] = {
    "Void", "Grass", "Water", "Rock", "Sand", "Tree"
};

static const char *const BIOME_NAMES[Z_COUNT] = {
    "Deep", "Shallow", "Beach", "Grassland", "Forest", "Mountain", "Snow"
};

/* --- STRUCTS ------------------------------------------------------------- */

/* World/model state: the tilemap, its backing buffer, and the wg_nf generator. */
typedef struct {
    rlk_tilemap_t tm;
    uint8_t buffer[TM_W * TM_H * sizeof(rlk_tile_t)];
    rlk_wg_nf_t wg;
    uint64_t seed;
} world_t;

/* Camera: position in world tile coords (float for smooth scrolling). */
typedef struct {
    float wx, wy;
} camera_t;

/* View state: camera, selection, display mode. */
typedef struct {
    camera_t cam;
    int selected_wx, selected_wy;  /* -1 if none */
    int display_mode;              /* DISPLAY_TILE or DISPLAY_BIOME */
} view_t;

/* UI resources + per-frame input snapshot. */
typedef struct {
    const pxl_font_t **fonts;
    size_t font_count;
    /* Input snapshot (filled by handle_input each frame) */
    int mx, my;
    bool mouse_left_down;
    bool mouse_left_clicked;
} ui_t;

/* Per-frame layout: pixel rects for each panel. */
typedef struct {
    pxl_rect_t root;
    pxl_rect_t map;        /* main column (left) for tilemap */
    pxl_rect_t minimap;    /* minimap area */
    pxl_rect_t view_mode;  /* view mode selector area */
    pxl_rect_t map_info;   /* map info area */
    pxl_rect_t legend;     /* legend area */
    pxl_rect_t tile_info;  /* tile info area */
} layout_t;

/* Minimap view: precomputed scale and offsets shared by render and input. */
typedef struct {
    float scale;
    int off_x, off_y;
    int pix_w, pix_h;  /* rendered minimap size in pixels */
} minimap_t;

/* Viewer state: aggregates everything the tilemap viewer needs. */
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

static void
write_deep(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->type = TILE_WATER;
    out->variant = (uint8_t)(ctx->depth * 3.0f); /* darker toward abyss */
}

static void
write_shallow(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)ctx; (void)data;
    out->type = TILE_WATER;
    out->variant = 3; /* lightest water shade */
}

static void
write_beach(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->type = TILE_SAND;
    out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
}

static void
write_grass(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)data;
    out->type = TILE_GRASS;
    out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
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
    /* Tree density: 30% at edge, 80% deep, modulated by detail noise */
    float density = (0.3f + ctx->depth * 0.5f) * (0.5f + ctx->field[F_DETAIL]);
    out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
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
    out->type = TILE_ROCK;
    out->flags = 0x01;
    out->variant = (uint8_t)(ctx->field[F_DETAIL] * 3.0f);
}

static void
write_snow(const rlk_wg_nf_ctx_t *ctx, const void *data, rlk_tile_t *out) {
    (void)ctx; (void)data;
    out->type = TILE_ROCK;
    out->flags = 0x01;
    out->variant = 3; /* snow: lightest rock shade */
}

/* --- WORLD GENERATION ---------------------------------------------------- */

static void
world_generate(world_t *w, uint64_t seed) {
    assert(w);
    w->seed = seed;

    rlk_tilemap_init(&w->tm, TM_W, TM_H, w->buffer, sizeof(w->buffer));

    rlk_wg_nf_t *wg = &w->wg;
    wg->seed = seed;

    /* Fields: elevation (continents), temperature, local detail */
    wg->nsampled = F_NFIELDS;
    wg->fields[F_ELEV]   = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 1, .octaves = 4, .freq = 0.015f, .gain = 0.5f };
    wg->fields[F_TEMP]   = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 2, .octaves = 3, .freq = 0.020f, .gain = 0.5f };
    wg->fields[F_DETAIL] = (rlk_wg_nf_field_t){ .kind = RLK_WG_NF_FBM, .seed = 3, .octaves = 2, .freq = 0.080f, .gain = 0.5f };

    /* Axes: elevation (0) + temperature (1), both with detail-based jitter */
    wg->naxes = 2;
    wg->axes[0] = (rlk_wg_nf_axis_t){ .field = F_ELEV,   .jitter_field = F_DETAIL, .jitter_amp = 0.03f };
    wg->axes[1] = (rlk_wg_nf_axis_t){ .field = F_TEMP,   .jitter_field = F_DETAIL, .jitter_amp = 0.03f };

    /* Zones: elevation slices, temperature splits lowland into grass/forest */
    wg->nzones = Z_COUNT;
    wg->zones[Z_DEEP]     = (rlk_wg_nf_zone_t){ .lo = {0.0f, 0.0f},  .hi = {ELEV_DEEP,     1.0f},  .write = write_deep };
    wg->zones[Z_SHALLOW]  = (rlk_wg_nf_zone_t){ .lo = {ELEV_DEEP, 0.0f},  .hi = {ELEV_SHALLOW,  1.0f},  .write = write_shallow };
    wg->zones[Z_BEACH]    = (rlk_wg_nf_zone_t){ .lo = {ELEV_SHALLOW, 0.0f}, .hi = {ELEV_BEACH,  1.0f},  .write = write_beach };
    wg->zones[Z_GRASS]    = (rlk_wg_nf_zone_t){ .lo = {ELEV_BEACH, 0.45f}, .hi = {ELEV_LOWLAND, 1.0f},  .write = write_grass };
    wg->zones[Z_FOREST]   = (rlk_wg_nf_zone_t){ .lo = {ELEV_BEACH, 0.0f},  .hi = {ELEV_LOWLAND, 0.45f}, .write = write_forest };
    wg->zones[Z_MOUNTAIN] = (rlk_wg_nf_zone_t){ .lo = {ELEV_LOWLAND, 0.0f}, .hi = {ELEV_MOUNTAIN, 1.0f}, .write = write_mountain };
    wg->zones[Z_SNOW]     = (rlk_wg_nf_zone_t){ .lo = {ELEV_MOUNTAIN, 0.0f}, .hi = {1.0f,        1.0f},  .write = write_snow };

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

    /* Split into main column (left) and sidebar (right) */
    l.map = l.root;
    pxl_rect_t sidebar = pxl_split_rect(&l.map, l.root.w * SIDEBAR_W_RATIO / 100, PXL_SIDE_RIGHT);

    /* Snap map to TILE_PX multiples so the tile grid stays pixel-aligned.
       Horizontal leftover goes to the sidebar; vertical leftover is centered
       (BG_COLOR fills the top/bottom bands). */
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

    /* Split sidebar: top = minimap, map info, tile info; bottom = legend, view_mode */
    l.minimap   = pxl_split_rect(&sidebar, sidebar.h * MINIMAP_H_RATIO / 100, PXL_SIDE_TOP);
    l.map_info  = pxl_split_rect(&sidebar, sidebar.h * MAP_INFO_H_RATIO / 100, PXL_SIDE_TOP);
    l.tile_info = pxl_split_rect(&sidebar, sidebar.h * TILE_INFO_H_RATIO / 100, PXL_SIDE_TOP);
    l.legend    = pxl_split_rect(&sidebar, l.tile_info.h, PXL_SIDE_BOTTOM);
    l.view_mode = pxl_split_rect(&sidebar, VIEW_MODE_BTN_H, PXL_SIDE_BOTTOM);

    return l;
}

/* --- MINIMAP VIEW -------------------------------------------------------- */

static minimap_t
minimap_view_compute(const pxl_rect_t *area, int map_w, int map_h) {
    assert(area && map_w > 0 && map_h > 0);

    minimap_t mm = {0};
    /* Uniform scale (min of both) to preserve the map aspect ratio */
    float scale_x = (float)area->w / (float)map_w;
    float scale_y = (float)area->h / (float)map_h;
    mm.scale = scale_x < scale_y ? scale_x : scale_y;
    mm.pix_w = (int)((float)map_w * mm.scale);
    mm.pix_h = (int)((float)map_h * mm.scale);
    mm.off_x = (area->w - mm.pix_w) / 2;
    mm.off_y = (area->h - mm.pix_h) / 2;
    return mm;
}

/* Convert minimap pixel coords to world tile coords (clamped to map bounds).
   Returns true if (mx, my) is within the minimap area. */
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

/* Immediate-mode button: draws label + outline if active, returns true if clicked. */
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

static void
render_tilemap(pxl_canvas_t *cnv, const viewer_t *a) {
    assert(cnv && a);

    const world_t *w = &a->world;
    const view_t *v = &a->view;

    pxl_rect_t view = pxl_canvas_view(cnv);
    int wx = (int)v->cam.wx;  /* world tile at camera top-left (integer part) */
    int wy = (int)v->cam.wy;
    /* Fractional part shifts the drawing origin; canvas scissor clips edges */
    int dx = -(int)((v->cam.wx - (float)wx) * (float)TILE_PX);
    int dy = -(int)((v->cam.wy - (float)wy) * (float)TILE_PX);
    /* +2 to cover the partially visible tiles at the edges */
    int vw = view.w / TILE_PX + 2;
    int vh = view.h / TILE_PX + 2;

    for (int y = 0; y < vh; y++) {       /* view-local row */
        for (int x = 0; x < vw; x++) {   /* view-local col */
            int twx = wx + x, twy = wy + y;  /* world tile coords */
            const rlk_tile_t *tile = rlk_tilemap_get(&w->tm, twx, twy);
            if (!tile) continue;  /* +2 may exceed map bounds at camera edges */

            int px = dx + x * TILE_PX;
            int py = dy + y * TILE_PX;

            pxl_canvas_set_color(cnv, v->display_mode == DISPLAY_BIOME
                ? get_biome_color(tile->biome)
                : TILE_COLORS[tile->type % TILE_COUNT]);
            pxl_fill_rect(cnv, px, py, TILE_PX, TILE_PX);
        }
    }

    /* Outline the selected tile (outside the tile loops) */
    if (v->selected_wx >= 0 && v->selected_wy >= 0) {
        int x = v->selected_wx - wx;  /* view-local coords of selection */
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

    pxl_rect_t view = pxl_canvas_view(cnv);
    int mm_w = view.w;
    int mm_h = view.h;

    for (int wy = 0; wy < w->tm.height; wy++) {
        for (int wx = 0; wx < w->tm.width; wx++) {
            const rlk_tile_t *tile = rlk_tilemap_get(&w->tm, wx, wy);

            int px = mm->off_x + (int)((float)wx * mm->scale);
            int py = mm->off_y + (int)((float)wy * mm->scale);
            int pw = (int)mm->scale + 1;
            int ph = (int)mm->scale + 1;

            if (px + pw > mm_w) pw = mm_w - px;
            if (py + ph > mm_h) ph = mm_h - py;

            pxl_canvas_set_color(cnv, TILE_COLORS[tile->type % TILE_COUNT]);
            pxl_fill_rect(cnv, px, py, pw, ph);
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

static void
render_view_mode(const ui_t *ui, pxl_canvas_t *cnv_tile, pxl_canvas_t *cnv_biome,
                 view_t *v) {
    assert(ui && cnv_tile && cnv_biome && v);

    if (ui_button(ui, cnv_tile, "Tile", v->display_mode == DISPLAY_TILE))
        v->display_mode = DISPLAY_TILE;
    if (ui_button(ui, cnv_biome, "Biome", v->display_mode == DISPLAY_BIOME))
        v->display_mode = DISPLAY_BIOME;
}

static void
render_map_info(pxl_canvas_t *cnv, const ui_t *ui, const viewer_t *a) {
    assert(cnv && ui && a);

    char nfo[255];
    snprintf(nfo, sizeof(nfo),
        "     MAP\n\n"
        "Size : %d x %d\n"
        "Tiles: %d\n"
        "Seed : %llu (R: renew)",
        TM_W, TM_H, TM_W * TM_H,
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

    const rlk_tile_t *tile = rlk_tilemap_get(&a->world.tm, v->selected_wx, v->selected_wy);

    char nfo[255];
    snprintf(nfo, sizeof(nfo),
        "     TILE\n\n"
        "Position: (%d,%d)\n"
        "Type    : %s (%d)\n"
        "Biome   : %s (%d)\n"
        "Variant : %d\n"
        "Flags   : 0x%02X",
        v->selected_wx, v->selected_wy,
        TILE_NAMES[tile->type % TILE_COUNT], tile->type,
        BIOME_NAMES[tile->biome % Z_COUNT], tile->biome,
        tile->variant, tile->flags);

    pxl_rect_t r = pxl_canvas_view(cnv);
    pxl_canvas_set_color(cnv, FG_COLOR);
    pxl_draw_rect(cnv, r.x, r.y, r.w, r.h);

    ui_draw_text(ui, cnv, nfo, PXL_ALIGN_V_CENTER | PXL_ALIGN_H_CENTER);
}

static void
render_legend(pxl_canvas_t *cnv, const ui_t *ui, const view_t *v) {
    assert(cnv && ui && v);

    pxl_rect_t view = pxl_canvas_view(cnv);

    /* Frame */
    pxl_canvas_set_color(cnv, FG_COLOR);
    pxl_draw_rect(cnv, view.x, view.y, view.w, view.h);

    pxl_writer_t w;
    pxl_writer_init(&w, ui->fonts, ui->font_count);

    const int swatch = 8;
    const int line_h = 14;
    const int gap    = 4;
    const int pad    = 4;
    int col_w = view.w / 2;
    /* Entries area: full height with 4px padding, vertically centered */
    int area_y = view.y + 4;
    int area_h = view.h - 8;

    if (v->display_mode == DISPLAY_BIOME) {
        /* 7 biomes over 2 columns: ceil(7/2)=4 left, 3 right */
        int per_col = (Z_COUNT + 1) / 2;
        int total_h = per_col * line_h;
        int y0 = area_y + (area_h - total_h) / 2;
        for (int i = 0; i < Z_COUNT; i++) {
            int col = i / per_col;
            int row = i % per_col;
            int x = view.x + col * col_w + pad;
            int y = y0 + row * line_h;
            pxl_canvas_set_color(cnv, get_biome_color((uint8_t)i));
            pxl_fill_rect(cnv, x, y + 3, swatch, swatch);
            pxl_writer_set_cursor(&w, x + swatch + gap, y);
            pxl_draw_text(cnv, &w, BIOME_NAMES[i]);
        }
    } else {
        /* 5 tile types (skip VOID) over 2 columns: ceil(5/2)=3 left, 2 right */
        int count = TILE_COUNT - 1;  /* skip VOID */
        int per_col = (count + 1) / 2;
        int total_h = per_col * line_h;
        int y0 = area_y + (area_h - total_h) / 2;
        for (int i = 0; i < count; i++) {
            int idx = i + 1;  /* start at GRASS (skip VOID) */
            int col = i / per_col;
            int row = i % per_col;
            int x = view.x + col * col_w + pad;
            int y = y0 + row * line_h;
            pxl_canvas_set_color(cnv, TILE_COLORS[idx]);
            pxl_fill_rect(cnv, x, y + 3, swatch, swatch);
            pxl_writer_set_cursor(&w, x + swatch + gap, y);
            pxl_draw_text(cnv, &w, TILE_NAMES[idx]);
        }
    }
}

/* --- INPUT --------------------------------------------------------------- */

static void
handle_input(pxl_app_t *pxl, viewer_t *a) {
    assert(pxl && a);

    /* Snapshot input into ui_t for the render phase */
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
                                 a->world.tm.width, a->world.tm.height,
                                 mx, my, &wx, &wy))
            camera_center_on(&a->view.cam, wx, wy, a->layout.map.w, a->layout.map.h);
    }

    /* Click: minimap center or tile selection */
    if (a->ui.mouse_left_clicked) {
        int wx, wy;
        if (minimap_view_to_tile(&a->mm, &a->layout.minimap,
                                 a->world.tm.width, a->world.tm.height,
                                 mx, my, &wx, &wy)) {
            camera_center_on(&a->view.cam, wx, wy, a->layout.map.w, a->layout.map.h);
        } else if (pt_in_rect(mx, my, a->layout.map)) {
            int twx = (int)(a->view.cam.wx + (float)(mx - a->layout.map.x) / (float)TILE_PX);
            int twy = (int)(a->view.cam.wy + (float)(my - a->layout.map.y) / (float)TILE_PX);

            if (twx >= 0 && twx < a->world.tm.width && twy >= 0 && twy < a->world.tm.height) {
                a->view.selected_wx = twx;
                a->view.selected_wy = twy;
            }
        }
    }

    /* Keep the camera inside the tilemap */
    camera_clamp(&a->view.cam, a->world.tm.width, a->world.tm.height,
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

    /* Frame around view_mode buttons */
    pxl_canvas_set_color(&cnv, FG_COLOR);
    pxl_draw_rect(&cnv, a->layout.view_mode.x, a->layout.view_mode.y,
                  a->layout.view_mode.w, a->layout.view_mode.h);

    pxl_rect_t vm_top = pxl_split_rect(&a->layout.view_mode,
                                        a->layout.view_mode.h / 2, PXL_SIDE_TOP);
    pxl_canvas_t cnv_map, cnv_minimap, cnv_vm_tile, cnv_vm_biome, cnv_map_info, cnv_legend, cnv_tile_info;
    pxl_canvas_init_view(&cnv_map, pb, a->layout.map);
    pxl_canvas_init_view(&cnv_minimap, pb, a->layout.minimap);
    pxl_canvas_init_view(&cnv_vm_tile, pb, vm_top);
    pxl_canvas_init_view(&cnv_vm_biome, pb, a->layout.view_mode);
    pxl_canvas_init_view(&cnv_map_info, pb, a->layout.map_info);
    pxl_canvas_init_view(&cnv_legend, pb, a->layout.legend);
    pxl_canvas_init_view(&cnv_tile_info, pb, a->layout.tile_info);

    render_tilemap(&cnv_map, a);
    render_minimap(&cnv_minimap, a);
    render_view_mode(&a->ui, &cnv_vm_tile, &cnv_vm_biome, &a->view);
    render_map_info(&cnv_map_info, &a->ui, a);
    render_legend(&cnv_legend, &a->ui, &a->view);
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
    a.view.display_mode = DISPLAY_TILE;

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
