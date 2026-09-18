#include <stdio.h>
#include <stdint.h>
#include "pxl.h"
#include "rlk.h"

/* ============================================================================
 * demo.c — pxl-based visualization of rlk region generation
 * 
 * Minimal, self-contained demo that:
 * 1. Generates a deterministic rlk region (tilemap + occ_mask)
 * 2. Renders it with pxl (SDL2) using a simple color palette
 * 3. Allows panning with arrow keys / HJKL
 * 4. Supports both 2D and isometric views
 * 
 * Build: make -C demo  (requires pxl and SDL2)
 * Run:   ./demo_region
 * 
 * Controls:
 *   HJKL / Arrow keys — pan the camera
 *   G                 — toggle grid overlay
 *   +/- / Mouse wheel — zoom in/out
 *   I                 — toggle isometric view
 *   R                 — regenerate with a new random seed
 *   Q / ESC          — quit
 * ============================================================================
 */

/* ---------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------------
 */

#define SCREEN_W   800
#define SCREEN_H   600
#define TILE_SZ    16      /* pixels per tile */
#define REGION_W   32      /* tiles */
#define REGION_H   24      /* tiles */

/* ---------------------------------------------------------------------------
 * Color palette for tile types (from rlk_region_default_terrain)
 * ---------------------------------------------------------------------------
 */

static const pxl_color_t g_palette[] = {
    [0]  = {  30,  30,  30, 255 },   /* VOID    (black-ish)    */
    [1]  = {  40,  80, 200, 255 },   /* WATER   (blue)         */
    [2]  = { 220, 190, 140, 255 },   /* SAND    (light brown) */
    [3]  = {  60, 180,  60, 255 },   /* GRASS   (green)        */
    [4]  = { 140,  90,  50, 255 },   /* DIRT    (brown)        */
    [5]  = { 120, 120, 120, 255 },   /* STONE   (gray)         */
};

/* ---------------------------------------------------------------------------
 * Global state
 * ---------------------------------------------------------------------------
 */

typedef struct {
    uint64_t        seed;           /* world seed */
    int32_t         region_x, region_y;
    rlk_tilemap_t   tm;
    uint8_t         buffer[RLK_TM_BUF_SIZE(REGION_W, REGION_H)];
    float           cam_x, cam_y;   /* camera position in world units */
    pxl_window_t   *win;
    pxl_renderer_t *ren;
    int             quit;
    int             show_grid;      /* toggle grid overlay */
    float           zoom;           /* render scale, 1.0 = 100% */
    int             isometric;      /* 0 = 2D, 1 = iso */
} DemoState;

static DemoState g;

/* ---------------------------------------------------------------------------
 * Convert tile coordinates to screen coordinates
 * ---------------------------------------------------------------------------
 */

static void demo_tile_to_screen(DemoState *d, int32_t x, int32_t y, float *sx, float *sy) {
    float z = d->zoom;
    if (!d->isometric) {
        *sx = (x * (float)TILE_SZ - d->cam_x) * z;
        *sy = (y * (float)TILE_SZ - d->cam_y) * z;
        return;
    }
    /* Isometric: diamond projection with 0.5 height factor */
    float iso_scale = (TILE_SZ / 2.0f) * z;
    *sx = (x - y) * iso_scale - d->cam_x * z;
    *sy = (x + y) * iso_scale * 0.5f - d->cam_y * z;
}

/* ---------------------------------------------------------------------------
 * Generate a new region
 * ---------------------------------------------------------------------------
 */

static void demo_generate_region(DemoState *d) {
    rlk_region_cfg_t cfg = {
        .world_seed = d->seed,
        .region_x   = d->region_x,
        .region_y   = d->region_y,
        .terrain    = NULL,  /* use default */
    };
    
    size_t buf_size = sizeof(d->buffer);
    size_t need     = rlk_tilemap_required_size(REGION_W, REGION_H);
    
    if (need > buf_size) {
        fprintf(stderr, "demo: buffer too small (%zu < %zu)\n", buf_size, need);
        return;
    }
    
    rlk_tilemap_init(&d->tm, REGION_W, REGION_H, (float)TILE_SZ,
                     d->buffer, buf_size);
    rlk_region_generate(&d->tm, &cfg);
}

/* ---------------------------------------------------------------------------
 * Random seed (simple xorshift64*)
 * ---------------------------------------------------------------------------
 */

static uint64_t demo_rand_seed(void) {
    static uint64_t s = 0xDEADBEEF;
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 0x2545F4914F6CDD1DULL;
}

/* ---------------------------------------------------------------------------
 * Draw the tilemap
 * ---------------------------------------------------------------------------
 */

static void demo_draw(DemoState *d) {
    pxl_clear(d->ren, (pxl_color_t){ 20, 20, 20, 255 });
    
    /* Camera offset for isometric view (center vertically) */
    float cam_y_offset = d->isometric ?
        (SCREEN_H / 2.0f) / (TILE_SZ * d->zoom) : 0.0f;
    
    /* Visible tile range (account for zoom in iso mode) */
    float tile_screen_w = (float)TILE_SZ * d->zoom;
    float tile_screen_h = (float)TILE_SZ * d->zoom * (d->isometric ? 0.5f : 1.0f);
    int32_t tx0 = (int32_t)((d->cam_x) / (float)TILE_SZ);
    int32_t ty0 = (int32_t)((d->cam_y + cam_y_offset) / (float)TILE_SZ);
    int32_t tx1 = tx0 + (int32_t)(SCREEN_W / tile_screen_w) + 2;
    int32_t ty1 = ty0 + (int32_t)(SCREEN_H / tile_screen_h) + 2;
    
    /* Clamp to region bounds */
    tx0 = tx0 < 0 ? 0 : (tx0 >= REGION_W ? REGION_W - 1 : tx0);
    ty0 = ty0 < 0 ? 0 : (ty0 >= REGION_H ? REGION_H - 1 : ty0);
    tx1 = tx1 > REGION_W ? REGION_W : tx1;
    ty1 = ty1 > REGION_H ? REGION_H : ty1;
    
    /* Draw tiles (reverse y order for isometric depth sorting) */
    int32_t y_start = d->isometric ? ty1 - 1 : ty0;
    int32_t y_end   = d->isometric ? ty0 - 1 : ty1;
    int32_t y_step  = d->isometric ? -1 : 1;
    
    for (int32_t y = y_start; y != y_end; y += y_step) {
        for (int32_t x = tx0; x < tx1; x++) {
            const rlk_tilecell_t *c = rlk_tilemap_get_c(&d->tm, x, y);
            if (!c) continue;
            
            pxl_color_t col = g_palette[c->type % (sizeof(g_palette)/sizeof(g_palette[0]))];
            
            /* Darken solid tiles slightly */
            if (c->flags & RLK_FLAG_SOLID) {
                col.r = (uint8_t)(col.r * 0.7f);
                col.g = (uint8_t)(col.g * 0.7f);
                col.b = (uint8_t)(col.b * 0.7f);
            }
            
            /* Tile position */
            float sx, sy;
            demo_tile_to_screen(d, x, y, &sx, &sy);
            
            pxl_draw_rect(d->ren, sx, sy, TILE_SZ * d->zoom, TILE_SZ * d->zoom, col);
            
            /* Draw grid lines (optional) */
            if (d->show_grid) {
                pxl_color_t grid_col = {80, 80, 80, 180};
                if (d->isometric) {
                    /* Iso grid: diagonal lines along tile edges */
                    float sx1, sy1, sx2, sy2;
                    demo_tile_to_screen(d, x+1, y,   &sx1, &sy1);
                    demo_tile_to_screen(d, x,   y+1, &sx2, &sy2);
                    pxl_draw_line(d->ren, sx, sy, sx1, sy1, grid_col);  /* right edge */
                    pxl_draw_line(d->ren, sx, sy, sx2, sy2, grid_col);  /* bottom edge */
                } else {
                    /* 2D grid: straight lines */
                    pxl_draw_rect(d->ren,
                        (x+1) * (float)TILE_SZ * d->zoom - d->cam_x * d->zoom,
                        y * (float)TILE_SZ * d->zoom - d->cam_y * d->zoom,
                        1.0f * d->zoom, TILE_SZ * d->zoom, grid_col);
                    pxl_draw_rect(d->ren,
                        x * (float)TILE_SZ * d->zoom - d->cam_x * d->zoom,
                        (y+1) * (float)TILE_SZ * d->zoom - d->cam_y * d->zoom,
                        TILE_SZ * d->zoom, 1.0f * d->zoom, grid_col);
                }
            }
        }
    }
    
    /* Draw UI overlay */
    char buf[128];
    snprintf(buf, sizeof(buf), "rlk demo | seed: 0x%016llX | region: (%d,%d)",
             (unsigned long long)d->seed, d->region_x, d->region_y);
    pxl_draw_text(d->ren, 10, 10, buf, (pxl_color_t){255,255,255,255});
    snprintf(buf, sizeof(buf), "HJKL/Arrows: pan  |  G: grid  |  +/-: zoom  |  I: iso  |  R: regen  |  Q/ESC: quit");
    pxl_draw_text(d->ren, 10, 30, buf, (pxl_color_t){200,200,200,255});
}

/* ---------------------------------------------------------------------------
 * Handle input
 * ---------------------------------------------------------------------------
 */

static void demo_handle_input(DemoState *d) {
    pxl_event_t ev;
    while (pxl_poll_event(&ev)) {
        switch (ev.type) {
        case PXL_EVENT_QUIT:
            d->quit = 1;
            break;
        case PXL_EVENT_MOUSE_WHEEL:
            if (ev.wheel.y > 0) {
                d->zoom = fminf(d->zoom * 1.1f, 4.0f);
            } else if (ev.wheel.y < 0) {
                d->zoom = fmaxf(d->zoom / 1.1f, 0.25f);
            }
            break;
        case PXL_EVENT_KEY_DOWN:
            switch (ev.key.keysym) {
            case PXL_KEY_ESCAPE:
            case PXL_KEY_q:
                d->quit = 1;
                break;
            case PXL_KEY_r:
                d->seed = demo_rand_seed();
                demo_generate_region(d);
                break;
            case PXL_KEY_i:
                d->isometric = !d->isometric;
                break;
            case PXL_KEY_k:
            case PXL_KEY_UP:
                d->cam_y -= 8.0f;
                break;
            case PXL_KEY_j:
            case PXL_KEY_DOWN:
                d->cam_y += 8.0f;
                break;
            case PXL_KEY_h:
            case PXL_KEY_LEFT:
                d->cam_x -= 8.0f;
                break;
            case PXL_KEY_l:
            case PXL_KEY_RIGHT:
                d->cam_x += 8.0f;
                break;
            case PXL_KEY_g:
                d->show_grid = !d->show_grid;
                break;  /* toggle grid */
            case PXL_KEY_PLUS:
                d->zoom = fminf(d->zoom * 1.1f, 4.0f);
                break;
            case PXL_KEY_MINUS:
                d->zoom = fmaxf(d->zoom / 1.1f, 0.25f);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------------
 */

int main(void) {
    /* Init pxl */
    if (pxl_init(PXL_INIT_VIDEO) < 0) {
        fprintf(stderr, "demo: pxl_init failed\n");
        return 1;
    }
    
    g.win = pxl_create_window("rlk demo", SCREEN_W, SCREEN_H, 0);
    if (!g.win) {
        fprintf(stderr, "demo: pxl_create_window failed\n");
        pxl_quit();
        return 1;
    }
    
    g.ren = pxl_create_renderer(g.win, 0);
    if (!g.ren) {
        fprintf(stderr, "demo: pxl_create_renderer failed\n");
        pxl_destroy_window(g.win);
        pxl_quit();
        return 1;
    }
    
    /* Init demo state */
    g.seed        = 0x123456789ABCDEF0ULL;
    g.region_x    = 0;
    g.region_y    = 0;
    g.cam_x       = 0.0f;
    g.cam_y       = 0.0f;
    g.quit        = 0;
    g.show_grid   = 0;    /* grid off by default */
    g.zoom        = 1.0f; /* default scale */
    g.isometric   = 0;    /* start in 2D mode */
    
    demo_generate_region(&g);
    
    /* Main loop */
    while (!g.quit) {
        demo_handle_input(&g);
        demo_draw(&g);
        pxl_render_present(g.ren);
    }
    
    /* Cleanup */
    pxl_destroy_renderer(g.ren);
    pxl_destroy_window(g.win);
    pxl_quit();
    
    return 0;
}
