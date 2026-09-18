/*
 * rlk Phase 0b demo — visualize a generated Tilemap with pxl.
 *
 * This is the bridge that freezes the rlk→pxl boundary:
 *   - rlk never includes pxl.h; only this demo does.
 *   - pxl never includes rlk internals; only the public rlk headers.
 *   - Memory is visible: the tilemap buffer is malloc'd by the demo and
 *     handed to rlk_tilemap_init (Pattern A). No allocation is hidden in rlk.
 *
 * Controls:
 *   Arrows      move between regions (region_x / region_y)
 *   R          new random world seed
 *   G          regenerate (no-op visually; same seed => identical image)
 *   ESC        quit
 *
 * The demo uses plain regeneration — no mutation exists in Phase 0, so the
 * honesty rule (load regenerated ⊕ RegionSaveState) does not apply yet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "pxl.h"

#include "rl/world/tilemap.h"
#include "rl/gen/region.h"

#define REG_W 64
#define REG_H 64
#define PIX   8          /* pixels per tile */

static uint64_t g_seed = 0xC0FFEEULL;
static int32_t  g_rx = 0, g_ry = 0;

/* One color per tile type (ARGB). Solid terrain gets a darker rim at draw. */
static pxl_t
tile_color(uint8_t type) {
    switch (type) {
    case RL_TILE_WATER: return 0xFF1B3A6B;
    case RL_TILE_SAND:   return 0xFFC2B280;
    case RL_TILE_GRASS:  return 0xFF4C8C2A;
    case RL_TILE_DIRT:   return 0xFF6B4A2B;
    case RL_TILE_STONE:  return 0xFF5A5A5A;
    default:             return 0xFF000000;   /* VOID */
    }
}

int
main(void) {
    pxl_app_t app = {
        .title  = "rlk Phase 0b — Tilemap",
        .width  = REG_W * PIX,
        .height = REG_H * PIX,
    };
    if (pxl_app_init(&app) != PXL_SUCCESS) {
        return 1;
    }

    /* Visible allocation: the demo owns the tilemap buffer (Pattern A). */
    size_t tm_size = rlk_tilemap_required_size(REG_W, REG_H);
    void *tm_buf = malloc(tm_size);
    if (tm_buf == NULL) {
        pxl_app_deinit(&app);
        return 1;
    }

    while (pxl_app_advance_wait(&app)) {
        if (pxl_app_was_pressed(&app, PXL_KEYB_ESCAPE)) break;

        if (pxl_app_was_pressed(&app, PXL_KEYB_R)) {
            g_seed = (uint64_t)time(NULL) ^ 0x9E3779B97F4A7C15ULL;
        }
        if (pxl_app_was_pressed(&app, PXL_KEYB_LEFT))  g_rx--;
        if (pxl_app_was_pressed(&app, PXL_KEYB_RIGHT)) g_rx++;
        if (pxl_app_was_pressed(&app, PXL_KEYB_UP))    g_ry--;
        if (pxl_app_was_pressed(&app, PXL_KEYB_DOWN))  g_ry++;
        /* G just regenerates the same region — same seed => identical. */
        if (pxl_app_was_pressed(&app, PXL_KEYB_G))    g_seed = g_seed;

        pxl_buf_t pb;
        if (pxl_backend_begin_frame(&pb) != PXL_SUCCESS) continue;

        /* Regenerate every frame (cheap at 64x64): same cfg => identical image. */
        rlk_tilemap_t tm;
        if (rlk_tilemap_init(&tm, REG_W, REG_H, 1.0f, tm_buf, tm_size) != RLK_OK) {
            continue;
        }
        rlk_region_cfg_t cfg = {
            .world_seed = g_seed,
            .region_x   = g_rx,
            .region_y   = g_ry,
        };
        rlk_region_generate(&tm, &cfg);

        pxl_canvas_t cnv;
        pxl_canvas_init(&cnv, &pb);
        pxl_canvas_set_color(&cnv, 0xFF101010);
        pxl_canvas_clear(&cnv);

        /* Draw each tile as a PIXxPIX square. Solid tiles get a darker rim. */
        for (int32_t y = 0; y < REG_H; y++) {
            for (int32_t x = 0; x < REG_W; x++) {
                rlk_tilecell_t *c = rlk_tilemap_get(&tm, x, y);
                if (c == NULL) continue;
                pxl_canvas_set_color(&cnv, tile_color(c->type));
                pxl_fill_rect(&cnv, x * PIX, y * PIX, PIX, PIX);
                if (rlk_tilemap_is_solid(&tm, x, y)) {
                    pxl_canvas_set_color(&cnv, 0xFF202020);
                    pxl_draw_rect(&cnv, x * PIX, y * PIX, PIX, PIX);
                }
            }
        }

        /* Overlay: region coords + seed. */
        char hud[96];
        snprintf(hud, sizeof hud, "seed=%016llX  region=(%d,%d)  [arrows/R/G/esc]",
                 (unsigned long long)g_seed, (int)g_rx, (int)g_ry);
        pxl_canvas_set_color(&cnv, 0xFFFFFFFF);
        pxl_draw_str(&cnv, 4, 4, hud);

        (void)pxl_backend_end_frame();
    }

    free(tm_buf);
    pxl_app_deinit(&app);
    return 0;
}
