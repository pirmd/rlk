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
 * 3. Allows panning with arrow keys / WASD
 * 4. Shows the region seed and coordinates
 * 
 * Build: make -C demo  (requires pxl and SDL2)
 * Run:   ./demo_region
 * 
 * Controls:
 *   WASD / Arrow keys — pan the camera
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
} DemoState;

static DemoState g;

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
    
    /* Compute visible tile range */
    int32_t tx0 = (int32_t)(d->cam_x / TILE_SZ);
    int32_t ty0 = (int32_t)(d->cam_y / TILE_SZ);
    int32_t tx1 = tx0 + (SCREEN_W / TILE_SZ) + 1;
    int32_t ty1 = ty0 + (SCREEN_H / TILE_SZ) + 1;
    
    /* Clamp to region bounds */
    tx0 = tx0 < 0 ? 0 : (tx0 >= REGION_W ? REGION_W - 1 : tx0);
    ty0 = ty0 < 0 ? 0 : (ty0 >= REGION_H ? REGION_H - 1 : ty0);
    tx1 = tx1 > REGION_W ? REGION_W : tx1;
    ty1 = ty1 > REGION_H ? REGION_H : ty1;
    
    /* Draw tiles */
    for (int32_t y = ty0; y < ty1; y++) {
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
            
            pxl_draw_rect(d->ren,
                (float)(x * TILE_SZ - d->cam_x),
                (float)(y * TILE_SZ - d->cam_y),
                (float)TILE_SZ, (float)TILE_SZ, col);
        }
    }
    
    /* Draw UI overlay */
    char buf[128];
    snprintf(buf, sizeof(buf), "rlk demo | seed: 0x%016llX | region: (%d,%d)",
             (unsigned long long)d->seed, d->region_x, d->region_y);
    pxl_draw_text(d->ren, 10, 10, buf, (pxl_color_t){255,255,255,255});
    snprintf(buf, sizeof(buf), "HJKL/Arrows: pan  |  R: regenerate  |  Q/ESC: quit");
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
    g.seed      = 0x123456789ABCDEF0ULL;
    g.region_x  = 0;
    g.region_y  = 0;
    g.cam_x     = 0.0f;
    g.cam_y     = 0.0f;
    g.quit      = 0;
    
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
