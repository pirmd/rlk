#ifndef WIDGET_H
#define WIDGET_H

/*
 * widget - reusable UI elements for PXL demos (demo component).
 *
 * Text, button, menu and legend on a pxl canvas. Stateless drawing
 * functions: colors and fonts come from the app (ui_t), no theme is
 * baked in. Header-only, POD, caller-owned, no allocation.
 *
 *   ui_t ui = { .fonts = (const pxl_font_t *[]){ &font_6x10 }, .font_count = 1 };
 *   widget_draw_text(&ui, cnv, "Hello", PXL_ALIGN_H_CENTER | PXL_ALIGN_V_CENTER);
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pxl.h"

/* UI resources + per-frame input snapshot (filled by viewer_ui_snapshot) */
typedef struct {
    const pxl_font_t **fonts;
    size_t font_count;
    int mx, my;
    bool mouse_left_down;
    bool mouse_left_clicked;
} ui_t;

/* 1 if (x, y) is inside rect r. */
static inline bool
widget_pt_in_rect(int x, int y, pxl_rect_t r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Draw text aligned within the canvas view. Uses the canvas color. */
static inline void
widget_draw_text(const ui_t *ui, pxl_canvas_t *cnv, const char *txt,
                 pxl_align_t align) {
    assert(cnv && ui && txt);

    pxl_rect_t bbox = pxl_canvas_view(cnv);

    pxl_writer_t w;
    pxl_writer_init(&w, ui->fonts, ui->font_count);

    pxl_rect_t bounds = pxl_text_bounds(&w, txt);

    pxl_rect_t aligned = pxl_align_rect(bounds, bbox, align);

    pxl_writer_set_cursor(&w, aligned.x, aligned.y);
    pxl_draw_text(cnv, &w, txt);
}

/* Draw a centered label, outlined with active_color if active.
   Returns 1 if the button was clicked this frame. */
static inline bool
widget_button(const ui_t *ui, pxl_canvas_t *cnv, const char *label,
              bool active, uint32_t active_color) {
    assert(ui && cnv && label);

    widget_draw_text(ui, cnv, label, PXL_ALIGN_H_CENTER | PXL_ALIGN_V_CENTER);

    if (active) {
        pxl_rect_t r = pxl_canvas_view(cnv);
        pxl_canvas_set_color(cnv, active_color);
        pxl_draw_rect(cnv, r.x, r.y, r.w, r.h);
    }

    return ui->mouse_left_clicked && widget_pt_in_rect(ui->mx, ui->my, cnv->scissor);
}

/* A bar of n equal cells, one label each, laid out horizontally or
   vertically to fill the canvas view. active: per-cell highlight flags
   (NULL = none). Returns the clicked cell index, or -1. */
static inline int
widget_menu(const ui_t *ui, pxl_canvas_t *cnv, const char *const *labels, int n,
            const bool *active, uint32_t active_color, bool horizontal) {
    assert(ui && cnv && labels && n > 0);

    pxl_rect_t abs = cnv->scissor;
    int d = horizontal ? abs.w / n : abs.h / n;
    int clicked = -1;
    for (int i = 0; i < n; i++) {
        pxl_rect_t cell = abs;
        if (horizontal) {
            cell.x = abs.x + i * d;
            cell.w = (i == n - 1) ? abs.w - i * d : d;
        } else {
            cell.y = abs.y + i * d;
            cell.h = (i == n - 1) ? abs.h - i * d : d;
        }
        pxl_canvas_t sub;
        pxl_canvas_init_view(&sub, cnv->pb, cell);
        if (widget_button(ui, &sub, labels[i], active ? active[i] : false,
                          active_color))
            clicked = i;
    }
    return clicked;
}

/* Grid menu: n labels in as many columns as fit (cells at least cell_w_min
   wide), rows sharing the canvas height. Returns the clicked cell index, or
   -1. Use for wide bars whose labels do not fit in a single row. */
static inline int
widget_menu_grid(const ui_t *ui, pxl_canvas_t *cnv, const char *const *labels,
                 int n, const bool *active, uint32_t active_color, int cell_w_min) {
    assert(ui && cnv && labels && n > 0 && cell_w_min > 0);

    pxl_rect_t abs = cnv->scissor;
    int cols = abs.w / cell_w_min;
    if (cols < 1) cols = 1;
    if (cols > n) cols = n;
    int rows = (n + cols - 1) / cols;
    int cw = abs.w / cols;
    int ch = abs.h / rows;
    int clicked = -1;
    for (int i = 0; i < n; i++) {
        int col = i % cols, row = i / cols;
        pxl_rect_t cell = { abs.x + col * cw, abs.y + row * ch, cw, ch };
        if (col == cols - 1) cell.w = abs.x + abs.w - cell.x;
        if (row == rows - 1) cell.h = abs.y + abs.h - cell.y;
        pxl_canvas_t sub;
        pxl_canvas_init_view(&sub, cnv->pb, cell);
        if (widget_button(ui, &sub, labels[i], active ? active[i] : false,
                          active_color))
            clicked = i;
    }
    return clicked;
}

/* One legend item: color swatch + label */
typedef struct {
    uint32_t color;
    const char *label;
} widget_item_t;

/* Legend columns for a view of width view_w (min WIDGET_LEGEND_COL_MIN px
   per column, so labels like "Lighthouse" fit next to the swatch). */
#define WIDGET_LEGEND_COL_MIN 96

static inline int
widget_legend_cols(int view_w) {
    int cols = view_w / WIDGET_LEGEND_COL_MIN;
    return cols > 0 ? cols : 1;
}

/* Draw n items column-major in widget_legend_cols columns, starting at y.
   Returns the y after the last row. The caller truncates n to the rows that
   fit the view height. */
static inline int
widget_legend(const ui_t *ui, pxl_canvas_t *cnv, int y,
              const widget_item_t *items, int n) {
    assert(ui && cnv && items && n > 0);

    pxl_rect_t view = pxl_canvas_view(cnv);

    pxl_writer_t w;
    pxl_writer_init(&w, ui->fonts, ui->font_count);

    const int swatch = 8;
    const int line_h = 14;
    const int gap    = 4;
    const int pad    = 4;
    int cols = widget_legend_cols(view.w);
    int rows = (n + cols - 1) / cols;
    int col_w = view.w / cols;

    for (int i = 0; i < n; i++) {
        int col = i / rows, row = i % rows;
        int x = col * col_w + pad;
        int yy = y + row * line_h;
        pxl_canvas_set_color(cnv, items[i].color);
        pxl_fill_rect(cnv, x, yy + 3, swatch, swatch);
        pxl_writer_set_cursor(&w, x + swatch + gap, yy);
        pxl_draw_text(cnv, &w, items[i].label);
    }
    return y + rows * line_h;
}

#endif /* WIDGET_H */
