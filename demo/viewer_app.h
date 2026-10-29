#ifndef VIEWER_APP_H
#define VIEWER_APP_H

/*
 * viewer_app - generic PXL viewer shell over a src_t source (demo layer 5).
 *
 * Owns the layout (map + sidebar: minimap, map info, tile info, legend,
 * mode bar, overlay bar), the input (camera, minimap pan, tile selection,
 * R = source regen) and the frame rendering. The map content, modes,
 * overlays and panels all come from the source callbacks (source.h).
 *
 *   static src_world_t world;  static src_t src;  static vapp_t a;
 *   src = src_world_source(&world);
 *   vapp_cfg_t cfg = { .src = &src, .title = "...", .win_w = 1024, ... };
 *   return viewer_app_run(&cfg, &a);
 *
 * Header-only, POD, caller-owned, no allocation.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "pxl.h"
#include "widget.h"
#include "viewer.h"
#include "source.h"

/* Chrome colors (0xAARRGGBB) */
#define VAPP_FG_COLOR        0xFFFFFFFFU
#define VAPP_BG_COLOR        0xFF101010U
#define VAPP_SELECT_COLOR    0xFF00FFFFU
#define VAPP_HIGHLIGHT_COLOR 0xFFFFFFFFU

/* Layout ratios (sidebar layout) */
#define VAPP_SIDEBAR_W_RATIO    25  /* sidebar takes 25% of window width */
#define VAPP_MINIMAP_H_RATIO    34  /* minimap takes 34% of sidebar height */
#define VAPP_MAP_INFO_H_RATIO   14  /* map info takes 14% of sidebar height */
#define VAPP_TILE_INFO_H_RATIO  28  /* tile info takes 28% (legend same, bottom) */
#define VAPP_BTN_H              22  /* button bar height (font 6x10 + padding) */

#define VAPP_MODE_MAX    16   /* max base modes in the mode bar */
#define VAPP_OVERLAY_MAX 16   /* max overlays in the overlay bar */
#define VAPP_LEGEND_MAX  32   /* legend item buffer */

/* Per-frame layout */
typedef struct {
    pxl_rect_t root;
    pxl_rect_t map;
    pxl_rect_t minimap;
    pxl_rect_t mode_bar;     /* base modes (radio) */
    pxl_rect_t overlay_bar;  /* overlays (toggle) */
    pxl_rect_t map_info;
    pxl_rect_t legend;
    pxl_rect_t tile_info;
} vlayout_t;

/* Run configuration (fonts, window, source) */
typedef struct {
    src_t *src;
    const char *title;
    int win_w, win_h;
    int tile_px;
    float cam_speed;   /* tiles per frame */
    const pxl_font_t **fonts;
    size_t font_count;
} vapp_cfg_t;

/* Viewer state (small; the big source state stays in src->data) */
typedef struct {
    src_t *src;
    int tile_px;
    float cam_speed;
    camera_t cam;
    ui_t ui;
    vlayout_t layout;
    minimap_t mm;
    int sel_wx, sel_wy;   /* selected tile, -1 = none */
    int mode;
    unsigned overlays;
} vapp_t;

/* --- LAYOUT -------------------------------------------------------------- */

static inline vlayout_t
vapp_compute_layout(int win_w, int win_h, int tile_px) {
    assert(win_w > 0 && win_h > 0 && tile_px > 0);

    vlayout_t l = {0};
    l.root.w = win_w;
    l.root.h = win_h;

    l.map = l.root;
    pxl_rect_t sidebar = pxl_split_rect(&l.map, l.root.w * VAPP_SIDEBAR_W_RATIO / 100, PXL_SIDE_RIGHT);

    int leftover_w = l.map.w % tile_px;
    if (leftover_w > 0) {
        l.map.w -= leftover_w;
        sidebar.x -= leftover_w;
        sidebar.w += leftover_w;
    }
    int leftover_h = l.map.h % tile_px;
    if (leftover_h > 0) {
        l.map.y += leftover_h / 2;
        l.map.h -= leftover_h;
    }

    l.minimap    = pxl_split_rect(&sidebar, sidebar.h * VAPP_MINIMAP_H_RATIO / 100, PXL_SIDE_TOP);
    l.map_info   = pxl_split_rect(&sidebar, sidebar.h * VAPP_MAP_INFO_H_RATIO / 100, PXL_SIDE_TOP);
    l.tile_info  = pxl_split_rect(&sidebar, sidebar.h * VAPP_TILE_INFO_H_RATIO / 100, PXL_SIDE_TOP);
    l.legend     = pxl_split_rect(&sidebar, l.tile_info.h, PXL_SIDE_BOTTOM);
    /* Two button bars at the bottom: overlays on bottom, modes above */
    l.overlay_bar = pxl_split_rect(&sidebar, VAPP_BTN_H, PXL_SIDE_BOTTOM);
    l.mode_bar    = pxl_split_rect(&sidebar, VAPP_BTN_H, PXL_SIDE_BOTTOM);

    return l;
}

/* --- RENDERING ----------------------------------------------------------- */

static inline void
vapp_render_map(pxl_canvas_t *cnv, vapp_t *a) {
    assert(cnv && a && a->src);

    src_t *s = a->src;
    pxl_rect_t view = pxl_canvas_view(cnv);
    int wx = (int)a->cam.wx;
    int wy = (int)a->cam.wy;
    int dx = -(int)((a->cam.wx - (float)wx) * (float)a->tile_px);
    int dy = -(int)((a->cam.wy - (float)wy) * (float)a->tile_px);
    int vw = view.w / a->tile_px + 2;
    int vh = view.h / a->tile_px + 2;

    for (int y = 0; y < vh; y++) {
        for (int x = 0; x < vw; x++) {
            int twx = wx + x, twy = wy + y;
            uint32_t color = s->color(s->data, a->mode, twx, twy);
            for (int o = 0; o < s->noverlays; o++)
                if ((a->overlays & (1u << o)) && s->overlay)
                    color = s->overlay(s->data, o, twx, twy, color);
            pxl_canvas_set_color(cnv, color);
            pxl_fill_rect(cnv, dx + x * a->tile_px, dy + y * a->tile_px,
                          a->tile_px, a->tile_px);
        }
    }

    src_view_t sv = { wx, wy, dx, dy, vw, vh, a->tile_px };
    for (int o = 0; o < s->noverlays; o++)
        if ((a->overlays & (1u << o)) && s->draw)
            s->draw(cnv, s->data, o, a->mode, &sv);

    /* Selected tile outline */
    if (a->sel_wx >= 0 && a->sel_wy >= 0) {
        int x = a->sel_wx - wx;
        int y = a->sel_wy - wy;
        if (x >= 0 && x < vw && y >= 0 && y < vh) {
            pxl_canvas_set_color(cnv, VAPP_SELECT_COLOR);
            pxl_draw_rect(cnv, dx + x * a->tile_px, dy + y * a->tile_px,
                          a->tile_px, a->tile_px);
        }
    }
}

static inline void
vapp_render_minimap(pxl_canvas_t *cnv, vapp_t *a) {
    assert(cnv && a && a->src);

    src_t *s = a->src;
    const minimap_t *mm = &a->mm;
    pxl_rect_t view = pxl_canvas_view(cnv);

    for (int wy = 0; wy < s->h; wy++) {
        for (int wx = 0; wx < s->w; wx++) {
            int px = mm->off_x + (int)((float)wx * mm->scale);
            int py = mm->off_y + (int)((float)wy * mm->scale);
            int pw = (int)mm->scale + 1;
            int ph = (int)mm->scale + 1;

            if (px + pw > view.w) pw = view.w - px;
            if (py + ph > view.h) ph = view.h - py;

            uint32_t color = s->color(s->data, a->mode, wx, wy);
            for (int o = 0; o < s->noverlays; o++)
                if ((a->overlays & (1u << o)) && s->overlay)
                    color = s->overlay(s->data, o, wx, wy, color);

            pxl_canvas_set_color(cnv, color);
            pxl_fill_rect(cnv, px, py, pw, ph);
        }
    }

    /* Viewport rectangle */
    float visible_w = (float)a->layout.map.w / (float)a->tile_px;
    float visible_h = (float)a->layout.map.h / (float)a->tile_px;

    pxl_canvas_set_color(cnv, VAPP_HIGHLIGHT_COLOR);
    pxl_draw_rect(cnv,
                  mm->off_x + (int)(a->cam.wx * mm->scale),
                  mm->off_y + (int)(a->cam.wy * mm->scale),
                  (int)(visible_w * mm->scale),
                  (int)(visible_h * mm->scale));

    /* Selected tile indicator */
    if (a->sel_wx >= 0 && a->sel_wy >= 0) {
        pxl_canvas_set_color(cnv, VAPP_SELECT_COLOR);
        pxl_draw_rect(cnv,
                      mm->off_x + (int)((float)a->sel_wx * mm->scale),
                      mm->off_y + (int)((float)a->sel_wy * mm->scale),
                      (int)mm->scale + 1,
                      (int)mm->scale + 1);
    }
}

/* Framed text panel (map info / tile info) */
static inline void
vapp_panel_text(pxl_canvas_t *cnv, const ui_t *ui, const char *txt) {
    assert(cnv && ui && txt);

    pxl_rect_t r = pxl_canvas_view(cnv);
    pxl_canvas_set_color(cnv, VAPP_FG_COLOR);
    pxl_draw_rect(cnv, r.x, r.y, r.w, r.h);

    widget_draw_text(ui, cnv, txt, PXL_ALIGN_V_CENTER | PXL_ALIGN_H_CENTER);
}

static inline void
vapp_render_legend(pxl_canvas_t *cnv, vapp_t *a) {
    assert(cnv && a && a->src);

    src_t *s = a->src;
    pxl_rect_t view = pxl_canvas_view(cnv);

    pxl_canvas_set_color(cnv, VAPP_FG_COLOR);
    pxl_draw_rect(cnv, view.x, view.y, view.w, view.h);

    widget_item_t items[VAPP_LEGEND_MAX];
    int n = 0;
    if (s->legend)
        n = s->legend(items, VAPP_LEGEND_MAX, s->data, a->mode, a->overlays);
    if (n <= 0)
        return;

    const int line_h = 14;
    int area_h = view.h - 8;
    int y = 4 + (area_h - n * line_h) / 2;
    (void)widget_legend(&a->ui, cnv, y, items, n, (n + 1) / 2);
}

static inline void
vapp_render_frame(pxl_buf_t *pb, vapp_t *a) {
    assert(pb && a && a->src);

    src_t *s = a->src;
    assert(s->nmodes <= VAPP_MODE_MAX && s->noverlays <= VAPP_OVERLAY_MAX);

    pxl_canvas_t cnv;
    pxl_canvas_init(&cnv, pb);
    pxl_canvas_set_color(&cnv, VAPP_BG_COLOR);
    pxl_canvas_clear(&cnv);

    /* Frames around button bars */
    pxl_canvas_set_color(&cnv, VAPP_FG_COLOR);
    pxl_draw_rect(&cnv, a->layout.mode_bar.x, a->layout.mode_bar.y,
                  a->layout.mode_bar.w, a->layout.mode_bar.h);
    pxl_draw_rect(&cnv, a->layout.overlay_bar.x, a->layout.overlay_bar.y,
                  a->layout.overlay_bar.w, a->layout.overlay_bar.h);

    pxl_canvas_t cnv_map, cnv_minimap, cnv_mode, cnv_ovl, cnv_map_info, cnv_legend, cnv_tile_info;
    pxl_canvas_init_view(&cnv_map, pb, a->layout.map);
    pxl_canvas_init_view(&cnv_minimap, pb, a->layout.minimap);
    pxl_canvas_init_view(&cnv_mode, pb, a->layout.mode_bar);
    pxl_canvas_init_view(&cnv_ovl, pb, a->layout.overlay_bar);
    pxl_canvas_init_view(&cnv_map_info, pb, a->layout.map_info);
    pxl_canvas_init_view(&cnv_legend, pb, a->layout.legend);
    pxl_canvas_init_view(&cnv_tile_info, pb, a->layout.tile_info);

    vapp_render_map(&cnv_map, a);
    vapp_render_minimap(&cnv_minimap, a);

    /* Mode bar (radio: one active at a time) */
    if (s->nmodes > 0) {
        bool active[VAPP_MODE_MAX];
        for (int i = 0; i < s->nmodes; i++)
            active[i] = (a->mode == i);
        int clicked = widget_menu(&a->ui, &cnv_mode, s->mode_names, s->nmodes,
                                  active, VAPP_SELECT_COLOR, true);
        if (clicked >= 0)
            a->mode = clicked;
    }

    /* Overlay bar (toggle: cumulative) */
    if (s->noverlays > 0) {
        bool active[VAPP_OVERLAY_MAX];
        for (int i = 0; i < s->noverlays; i++)
            active[i] = (a->overlays & (1u << i)) != 0;
        int clicked = widget_menu(&a->ui, &cnv_ovl, s->overlay_names, s->noverlays,
                                  active, VAPP_SELECT_COLOR, true);
        if (clicked >= 0)
            a->overlays ^= (1u << clicked);
    }

    /* Sidebar panels */
    char buf[512];
    if (s->map_info) {
        s->map_info(buf, sizeof(buf), s->data);
        vapp_panel_text(&cnv_map_info, &a->ui, buf);
    }
    vapp_render_legend(&cnv_legend, a);
    if (s->tile_info && a->sel_wx >= 0 && a->sel_wy >= 0) {
        s->tile_info(buf, sizeof(buf), s->data, a->sel_wx, a->sel_wy);
        vapp_panel_text(&cnv_tile_info, &a->ui, buf);
    }
}

/* --- INPUT --------------------------------------------------------------- */

static inline void
vapp_input(pxl_app_t *pxl, vapp_t *a) {
    assert(pxl && a && a->src);

    src_t *s = a->src;

    viewer_ui_snapshot(pxl, &a->ui);
    viewer_camera_move(pxl, &a->cam, a->cam_speed);

    /* Regenerate (R) */
    if (pxl_app_was_triggered(pxl, PXL_KEYB_R) && s->regen) {
        s->regen(s->data);
        a->sel_wx = -1;
        a->sel_wy = -1;
    }

    int mx = a->ui.mx;
    int my = a->ui.my;

    /* Minimap drag: hold left click to pan camera */
    if (a->ui.mouse_left_down) {
        int wx, wy;
        if (viewer_minimap_to_tile(&a->mm, &a->layout.minimap,
                                   s->w, s->h, mx, my, &wx, &wy))
            viewer_camera_center_on(&a->cam, wx, wy,
                                    a->layout.map.w, a->layout.map.h, a->tile_px);
    }

    /* Click: minimap center or tile selection */
    if (a->ui.mouse_left_clicked) {
        int wx, wy;
        if (viewer_minimap_to_tile(&a->mm, &a->layout.minimap,
                                   s->w, s->h, mx, my, &wx, &wy)) {
            viewer_camera_center_on(&a->cam, wx, wy,
                                    a->layout.map.w, a->layout.map.h, a->tile_px);
        } else if (widget_pt_in_rect(mx, my, a->layout.map)) {
            int twx = (int)(a->cam.wx + (float)(mx - a->layout.map.x) / (float)a->tile_px);
            int twy = (int)(a->cam.wy + (float)(my - a->layout.map.y) / (float)a->tile_px);

            if (twx >= 0 && twx < s->w && twy >= 0 && twy < s->h) {
                a->sel_wx = twx;
                a->sel_wy = twy;
            }
        }
    }

    viewer_camera_clamp(&a->cam, s->w, s->h,
                        a->layout.map.w, a->layout.map.h, a->tile_px);
}

/* --- MAIN LOOP ----------------------------------------------------------- */

static inline int
viewer_app_run(const vapp_cfg_t *cfg, vapp_t *a) {
    assert(cfg && cfg->src && cfg->title && cfg->fonts && cfg->font_count > 0);
    assert(cfg->win_w > 0 && cfg->win_h > 0 && cfg->tile_px > 0);

    pxl_app_cfg_t pcfg = {
        .title = cfg->title,
        .width = cfg->win_w,
        .height = cfg->win_h,
        .backend_flags = PXL_BACKEND_RESIZABLE,
    };

    pxl_app_t app;
    if (pxl_app_init(&app, &pcfg) != PXL_SUCCESS) return 1;

    a->src = cfg->src;
    a->tile_px = cfg->tile_px;
    a->cam_speed = cfg->cam_speed;
    a->ui.fonts = cfg->fonts;
    a->ui.font_count = cfg->font_count;
    a->cam.wx = 0.0f;
    a->cam.wy = 0.0f;
    a->sel_wx = -1;
    a->sel_wy = -1;
    a->mode = cfg->src->def_mode;
    a->overlays = cfg->src->def_overlays;

    while (pxl_app_advance_wait(&app)) {
        if (pxl_app_was_triggered(&app, PXL_KEYB_ESCAPE))
            break;

        int win_w, win_h;
        pxl_backend_get_window_size(&win_w, &win_h);
        a->layout = vapp_compute_layout(win_w, win_h, a->tile_px);
        a->mm = viewer_minimap_compute(&a->layout.minimap, a->src->w, a->src->h);

        vapp_input(&app, a);

        pxl_buf_t pb;
        if (pxl_backend_begin_frame(&pb) == PXL_SUCCESS) {
            vapp_render_frame(&pb, a);
            (void)pxl_backend_end_frame();
        }
    }

    pxl_app_deinit(&app);
    return 0;
}

#endif /* VIEWER_APP_H */
