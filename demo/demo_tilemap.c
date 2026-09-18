/*
 * rlk Phase 0b demo — visualize a generated Tilemap with pxl.
 *
 * This is the bridge that freezes the rlk→pxl boundary:
 *   - rlk never includes pxl.h; only this demo does.
 *   - pxl never includes rlk internals; only the public rlk headers.
 *   - Memory is visible: the tilemap buffer is malloc'd by the demo and
 *     handed to rlk_tilemap_init (Pattern A). No allocation is hidden in rlk.
 *
 * The map is larger than the window: an rlk_view (clamped camera, no alloc)
 * pans over it. Same seed => identical map; arrows pan the view, R re-rolls
 * seed, G regenerates, ESC quits.
 *
 * Uses plain regeneration — no mutation exists in Phase 0, so the honesty
 * rule (load regenerated ⊕ RegionSaveState) does not apply yet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "pxl.h"

#include "rl/world/tilemap.h"
#include "rl/world/view.h"
#include "rl/gen/region.h"

#define MAP_W  128
#define MAP_H  96
#define VIEW_CW 64
#define VIEW_CH 48
#define PIX    12

static uint64_t g_seed = 0xC0FFEEULL;
static int32_t  g_rx = 0, g_ry = 0;

static pxl_t
tile_color(uint8_t type) {
    switch (type) {
    case RL_TILE_WATER: return 0xFF1B3A6B;
    case RL_TILE_SAND:   return 0xFFC2B280;
    case RL_TILE_GRASS:  return 0xFF4C8C2A;
    case RL_TILE_DIRT:   return 0xFF6B4A2B;
    case RL_TILE_STONE:  return 0xFF5A5A5A;
    default:             return 0xFF000000;
    }
}

int
main(void) {
    pxl_app_t app = {
        .title  = "rlk Phase 0b — Tilemap + view",
        .width  = VIEW_CW * PIX,
        .height = VIEW_CH * PIX,
    };
    if (pxl_app_init(&app) != PXL_SUCCESS) {
        return 1;
    }

    size_t tm_size = rlk_tilemap_required_size(MAP_W, MAP_H);
    void *tm_buf = malloc(tm_size);
    if (tm_buf == NULL) {
        pxl_app_deinit(&app);
        return 1;
    }

    /* Generate once per (seed, region); the view pans over it. */
    rlk_tilemap_t tm;
    rlk_region_cfg_t cfg;
    rlk_view_t view;

    int need_regen = 1;

    while (pxl_app_advance_wait(&app)) {
        if (pxl_app_was_pressed(&app, PXL_KEYB_ESCAPE)) break;

        if (pxl_app_was_pressed(&app, PXL_KEYB_R)) {
            g_seed = (uint64_t)time(NULL) ^ 0x9E3779B97F4A7C15ULL;
            need_regen = 1;
        }
        if (pxl_app_was_pressed(&app, PXL_KEYB_G)) {
            need_regen = 1;   /* regenerate the same seed => identical */
        }

        pxl_buf_t pb;
        if (pxl_backend_begin_frame(&pb) != PXL_SUCCESS) continue;

        if (need_regen) {
            if (rlk_tilemap_init(&tm, MAP_W, MAP_H, 1.0f, tm_buf, tm_size) != RLK_OK) {
                continue;
            }
            cfg.world_seed = g_seed;
            cfg.region_x   = g_rx;
            cfg.region_y   = g_ry;
            rlk_region_generate(&tm, &cfg);
            rlk_view_init(&view, &tm, 0, 0, VIEW_CW, VIEW_CH);
            rlk_view_center(&view, MAP_W / 2, MAP_H / 2);
            need_regen = 0;
        }

        if (pxl_app_is_pressed(&app, PXL_KEYB_LEFT))  rlk_view_move(&view, -1, 0);
        if (pxl_app_is_pressed(&app, PXL_KEYB_RIGHT)) rlk_view_move(&view, 1, 0);
        if (pxl_app_is_pressed(&app, PXL_KEYB_UP))    rlk_view_move(&view, 0, -1);
        if (pxl_app_is_pressed(&app, PXL_KEYB_DOWN))  rlk_view_move(&view, 0, 1);

        pxl_canvas_t cnv;
        pxl_canvas_init(&cnv, &pb);
        pxl_canvas_set_color(&cnv, 0xFF101010);
        pxl_canvas_clear(&cnv);

        for (int vy = 0; vy < view.vh; vy++) {
            for (int vx = 0; vx < view.vw; vx++) {
                rlk_tilecell_t *c = rlk_view_get(&view, vx, vy);
                if (c == NULL) continue;
                int x = vx * PIX;
                int y = vy * PIX;
                pxl_canvas_set_color(&cnv, tile_color(c->type));
                pxl_fill_rect(&cnv, x, y, PIX, PIX);
                if (rlk_view_is_solid(&view, vx, vy)) {
                    pxl_canvas_set_color(&cnv, 0xFF202020);
                    pxl_draw_rect(&cnv, x, y, PIX, PIX);
                }
            }
        }

        char hud[128];
        snprintf(hud, sizeof hud,
                 "seed=%016llX  region=(%d,%d)  view origin=(%d,%d)  [arrows/R/G/esc]",
                 (unsigned long long)g_seed, (int)g_rx, (int)g_ry,
                 view.ox, view.oy);
        pxl_canvas_set_color(&cnv, 0xFFFFFFFF);
        pxl_draw_str(&cnv, 4, 4, hud);

        (void)pxl_backend_end_frame();
    }

    free(tm_buf);
    pxl_app_deinit(&app);
    return 0;
}
