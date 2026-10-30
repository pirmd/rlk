#ifndef VIEWER_H
#define VIEWER_H

/*
 * viewer - reusable view bricks for PXL tilemap viewers (demo component).
 *
 * World-tile camera, minimap view math and per-frame input snapshot.
 * The layout, palette and map rendering stay in the app; widgets come
 * from widget.h. Header-only, POD, caller-owned, no allocation.
 *
 *   static app_t a;   <- the app composes camera_t + ui_t + its own layout
 *   viewer_ui_snapshot(pxl, &a.ui);
 *   viewer_camera_move(pxl, &a.cam, CAM_SPEED);
 *   a.mm = viewer_minimap_compute(&a.layout.minimap, map_w, map_h);
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "pxl.h"
#include "widget.h"

/* --- CAMERA -------------------------------------------------------------- */

/* World-tile camera: wx, wy is the top-left visible tile (float, sub-tile
   precision for smooth scrolling). */
typedef struct {
    float wx, wy;
} camera_t;

/* Center the camera on world tile (wx, wy); view_w/h in pixels. */
static inline void
viewer_camera_center_on(camera_t *cam, int wx, int wy, int view_w, int view_h,
                        int tile_px) {
    assert(cam);
    cam->wx = (float)wx - (float)view_w / (2.0f * (float)tile_px);
    cam->wy = (float)wy - (float)view_h / (2.0f * (float)tile_px);
}

/* Clamp the camera so the view stays inside the map (map_w/h in tiles,
   view_w/h in pixels). */
static inline void
viewer_camera_clamp(camera_t *cam, int map_w, int map_h, int view_w, int view_h,
                    int tile_px) {
    assert(cam);
    float max_x = (float)map_w - (float)view_w / (float)tile_px;
    float max_y = (float)map_h - (float)view_h / (float)tile_px;
    if (max_x < 0.0f) max_x = 0.0f;
    if (max_y < 0.0f) max_y = 0.0f;
    if (cam->wx < 0.0f) cam->wx = 0.0f; else if (cam->wx > max_x) cam->wx = max_x;
    if (cam->wy < 0.0f) cam->wy = 0.0f; else if (cam->wy > max_y) cam->wy = max_y;
}

/* Camera movement: arrows + hjkl, cam_speed in tiles per frame. */
static inline void
viewer_camera_move(pxl_app_t *pxl, camera_t *cam, float cam_speed) {
    assert(pxl && cam);
    if (pxl_app_is_active(pxl, PXL_KEYB_LEFT)  || pxl_app_is_active(pxl, PXL_KEYB_H)) cam->wx -= cam_speed;
    if (pxl_app_is_active(pxl, PXL_KEYB_RIGHT) || pxl_app_is_active(pxl, PXL_KEYB_L)) cam->wx += cam_speed;
    if (pxl_app_is_active(pxl, PXL_KEYB_UP)    || pxl_app_is_active(pxl, PXL_KEYB_K)) cam->wy -= cam_speed;
    if (pxl_app_is_active(pxl, PXL_KEYB_DOWN)  || pxl_app_is_active(pxl, PXL_KEYB_J)) cam->wy += cam_speed;
}

/* --- MINIMAP VIEW -------------------------------------------------------- */

/* Fit of a map (map_w x map_h tiles) into an area rect: uniform scale,
   centered. */
typedef struct {
    float scale;
    int off_x, off_y;
    int pix_w, pix_h;
} minimap_t;

static inline minimap_t
viewer_minimap_compute(const pxl_rect_t *area, int map_w, int map_h) {
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

/* Map a mouse position (mx, my, absolute pixels) to a world tile inside
   the minimap area. Returns 0 if the mouse is outside the area. */
static inline bool
viewer_minimap_to_tile(const minimap_t *mm, const pxl_rect_t *area,
                       int map_w, int map_h,
                       int mx, int my, int *out_wx, int *out_wy) {
    assert(mm && area && map_w > 0 && map_h > 0 && out_wx && out_wy);

    if (!widget_pt_in_rect(mx, my, *area))
        return false;

    *out_wx = (int)((float)(mx - area->x - mm->off_x) / mm->scale);
    *out_wy = (int)((float)(my - area->y - mm->off_y) / mm->scale);

    if (*out_wx < 0) *out_wx = 0;
    if (*out_wx >= map_w) *out_wx = map_w - 1;
    if (*out_wy < 0) *out_wy = 0;
    if (*out_wy >= map_h) *out_wy = map_h - 1;

    return true;
}

/* --- INPUT SNAPSHOT ------------------------------------------------------ */

/* Per-frame input snapshot into ui_t: mouse position + left button. */
static inline void
viewer_ui_snapshot(pxl_app_t *pxl, ui_t *ui) {
    assert(pxl && ui);

    ui->mx = pxl->curr.mouse_x;
    ui->my = pxl->curr.mouse_y;
    ui->mouse_left_down = pxl_app_is_active(pxl, PXL_MOUSE_LEFT);
    ui->mouse_left_clicked = pxl_app_was_triggered(pxl, PXL_MOUSE_LEFT);
}

#endif /* VIEWER_H */
