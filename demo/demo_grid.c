/*
 * RLK demo: render a grid with PXL.
 *
 * Builds a small dungeon-like grid (walls + floor + scattered "gold"),
 * centers a view on it, and renders it cell-by-cell using PXL primitives.
 * Arrow keys pan the view, ESC quits.
 *
 * RLK is graphics-backend agnostic; this demo is the bridge to PXL: it maps
 * each rlk_view cell to a destination tile rectangle and asks PXL to draw it.
 */
#include "rlk.h"
#include "pxl.h"

/* Tile kinds (game-defined; RLK does not interpret them). */
enum {
    TILE_FLOOR = 1,
    TILE_WALL  = 2,
    TILE_GOLD  = 3,
};

#define MAP_W 40
#define MAP_H 30
#define TILE_PX 16  /* destination tile size in pixels */
#define VIEW_CW 32  /* view size in cells */
#define VIEW_CH 28

static rlk_grid_t g_map;

/* Tile color table for rendering (ARGB). Index by tile kind. */
static pxl_t tile_color(rlk_tile_t t) {
    switch (t) {
    case TILE_WALL:  return 0xFF3A3A4A; /* dark slate */
    case TILE_FLOOR: return 0xFF1C1C28; /* near-black floor */
    case TILE_GOLD:  return 0xFFFFD24A; /* gold */
    default:         return 0xFF0A0A12; /* void */
    }
}

/* Build a simple bordered map with a few rooms and scattered gold. */
static void
build_map(rlk_grid_t *g) {
    rlk_grid_fill(g, TILE_FLOOR);
    /* border walls */
    rlk_grid_fill_rect(g, 0, 0, MAP_W, 1, TILE_WALL);
    rlk_grid_fill_rect(g, 0, MAP_H - 1, MAP_W, 1, TILE_WALL);
    rlk_grid_fill_rect(g, 0, 0, 1, MAP_H, TILE_WALL);
    rlk_grid_fill_rect(g, MAP_W - 1, 0, 1, MAP_H, TILE_WALL);
    /* a couple of rooms */
    rlk_grid_fill_rect(g, 4, 4, 8, 6, TILE_WALL);
    rlk_grid_fill_rect(g, 25, 18, 9, 7, TILE_WALL);
    /* some pillars */
    rlk_grid_fill_rect(g, 14, 12, 2, 2, TILE_WALL);
    rlk_grid_fill_rect(g, 20, 8, 2, 2, TILE_WALL);
    /* scattered gold */
    rlk_grid_set(g, 10, 10, TILE_GOLD);
    rlk_grid_set(g, 30, 5, TILE_GOLD);
    rlk_grid_set(g, 18, 22, TILE_GOLD);
    rlk_grid_set(g, 7, 25, TILE_GOLD);
}

/* Render the visible portion of the grid through the view, with PXL.
 * Each destination tile is TILE_PX square, drawn at (vx*TW, vy*TH). */
static void
render(pxl_canvas_t *cnv, const rlk_view_t *v) {
    for (int vy = 0; vy < v->vh; ++vy) {
        for (int vx = 0; vx < v->vw; ++vx) {
            rlk_tile_t t = rlk_view_get(v, vx, vy);
            int x = vx * TILE_PX;
            int y = vy * TILE_PX;
            pxl_canvas_set_color(cnv, tile_color(t));
            pxl_fill_rect(cnv, x, y, TILE_PX, TILE_PX);
            /* subtle grid lines */
            pxl_canvas_set_color(cnv, 0xFF141420);
            pxl_draw_rect(cnv, x, y, TILE_PX, TILE_PX);
        }
    }
}

int
main(void) {
    if (rlk_grid_init(&g_map, MAP_W, MAP_H) != RLK_SUCCESS)
        return 1;
    build_map(&g_map);

    pxl_app_t app = {
        .title  = "RLK grid demo - PXL",
        .width  = VIEW_CW * TILE_PX,
        .height = VIEW_CH * TILE_PX,
    };
    if (pxl_app_init(&app) != PXL_SUCCESS) {
        rlk_grid_deinit(&g_map);
        return 1;
    }

    rlk_view_t view;
    if (rlk_view_init(&view, &g_map, 0, 0, VIEW_CW, VIEW_CH) != RLK_SUCCESS) {
        pxl_app_deinit(&app);
        rlk_grid_deinit(&g_map);
        return 1;
    }
    rlk_view_center(&view, MAP_W / 2, MAP_H / 2);

    while (pxl_app_advance_wait(&app)) {
        if (pxl_app_was_pressed(&app, PXL_KEYB_ESCAPE))
            break;

        if (pxl_app_was_pressed(&app, PXL_KEYB_LEFT))  rlk_view_move(&view, -1, 0);
        if (pxl_app_was_pressed(&app, PXL_KEYB_RIGHT)) rlk_view_move(&view, 1, 0);
        if (pxl_app_was_pressed(&app, PXL_KEYB_UP))    rlk_view_move(&view, 0, -1);
        if (pxl_app_was_pressed(&app, PXL_KEYB_DOWN))  rlk_view_move(&view, 0, 1);

        pxl_buf_t pb;
        if (pxl_backend_begin_frame(&pb) == PXL_SUCCESS) {
            pxl_canvas_t cnv;
            pxl_canvas_init(&cnv, &pb);
            pxl_canvas_set_color(&cnv, 0xFF0A0A12);
            pxl_canvas_clear(&cnv);
            render(&cnv, &view);

            pxl_canvas_set_color(&cnv, 0xFFAAAAAA);
            pxl_draw_str(&cnv, 8, 8, "RLK + PXL - arrows to pan, ESC to quit");
            (void)pxl_backend_end_frame();
        }
    }

    pxl_app_deinit(&app);
    rlk_grid_deinit(&g_map);
    return 0;
}
