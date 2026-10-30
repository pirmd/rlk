#ifndef SOURCE_H
#define SOURCE_H

/*
 * source - viewer source interface for demo_viewer (demo component).
 *
 * A source is POD state + callbacks; the generic viewer shell (viewer_app.h)
 * renders any source through this interface. The shell owns the layout,
 * camera, input and sidebar chrome; the source owns the map content, its
 * display modes/overlays and its info panels.
 *
 *   static src_world_t world;
 *   src_world_init(&world, seed);
 *   src_t src = src_world_source(&world);
 *   viewer_app_run(&cfg_with_src, &app);
 *
 * Header-only, POD, caller-owned, no allocation. Any callback except
 * `color` may be NULL (feature skipped).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pxl.h"
#include "widget.h"

/* Map view geometry passed to draw callbacks: which world tiles are visible
   and where they land on the map canvas (view-local pixels). */
typedef struct {
    int wx, wy;    /* first visible world tile (top-left) */
    int dx, dy;    /* pixel position of tile (wx,wy) on the map canvas */
    int vw, vh;    /* visible tile counts */
    int tile_px;   /* screen pixels per tile */
} src_view_t;

typedef struct {
    const char *name;       /* source name (window title, map info) */
    void *data;             /* POD source state, caller-owned */
    int w, h;               /* map size in tiles */
    uint32_t bg;            /* color for out-of-map tiles */
    int def_mode;           /* initial mode */
    unsigned def_overlays;  /* initial overlay bitmask */

    /* Base modes (radio buttons, one active) */
    int nmodes;
    const char *const *mode_names;
    uint32_t (*color)(void *data, int mode, int wx, int wy);

    /* Overlays (toggle buttons, overlay o = bit 1<<o) */
    int noverlays;
    const char *const *overlay_names;
    /* Recolor tile (wx,wy) while overlay o is active; return base to keep. */
    uint32_t (*overlay)(void *data, int o, int wx, int wy, uint32_t base);
    /* Draw markers for overlay o on the map canvas, after the tiles.
       mode = the active base mode, so overlays can adapt to it. */
    void (*draw)(pxl_canvas_t *cnv, void *data, int o, int mode, const src_view_t *view);

    /* Sidebar panels: fill buf with text (may contain \n) */
    void (*map_info)(char *buf, size_t cap, const void *data);
    void (*tile_info)(char *buf, size_t cap, const void *data, int wx, int wy);
    /* Fill legend items for the current mode/overlays; returns the count. */
    int (*legend)(widget_item_t *items, int cap, const void *data,
                  int mode, unsigned overlays);

    /* Regenerate the source (R key). NULL = R does nothing. */
    void (*regen)(void *data);
} src_t;

#endif /* SOURCE_H */
