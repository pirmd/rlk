/*
 * Generic tilemap viewer: one PXL shell (viewer_app.h) over pluggable
 * sources (source.h). Select the source by argv:
 *
 *   demo_viewer [world|ca|bsp|nav] [seed]
 *
 *   world  tilemap_gen fantasy map (modes: type/biome/variant/depth +
 *          stages elev/relief/flow/lake/cont/temp/hum/slope/strahler;
 *          overlays: river/detail/POI)
 *   ca     cellular-automaton cave (rlk_ca_fill + entrance POI)
 *   bsp    BSP dungeon (rlk_bsp_fill + spawn POI)
 *   nav    in-game navigation scenario (mask + A* + overlays)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxl.h"
#include "font_6x10.h"

#include "source.h"
#include "viewer_app.h"
#include "src_world.h"
#include "src_dungeon.h"
#include "src_nav.h"

#define W 1024
#define H 768
#define TILE_PX   16
#define CAM_SPEED 24.0f
#define DEFAULT_SEED 42ULL

static const pxl_font_t *FONTS[] = { &font_6x10 };

int
main(int argc, char **argv) {
    const char *which = (argc > 1) ? argv[1] : "world";
    uint64_t seed = (argc > 2) ? strtoull(argv[2], NULL, 0) : DEFAULT_SEED;

    static vapp_t a;
    vapp_cfg_t cfg = {
        .win_w = W,
        .win_h = H,
        .tile_px = TILE_PX,
        .cam_speed = CAM_SPEED,
        .fonts = FONTS,
        .font_count = 1,
    };

    if (!strcmp(which, "world")) {
        static src_world_t world;
        static src_t src;
        src_world_init(&world, seed);
        src = src_world_source(&world);
        cfg.src = &src;
        cfg.title = "RLK demo_viewer - world";
    } else if (!strcmp(which, "ca") || !strcmp(which, "bsp")) {
        static src_dungeon_t dun;
        static src_t src;
        src_dungeon_init(&dun, !strcmp(which, "bsp") ? DUN_SRC_BSP : DUN_SRC_CA, seed);
        src = src_dungeon_source(&dun);
        cfg.src = &src;
        cfg.title = !strcmp(which, "bsp") ? "RLK demo_viewer - bsp" : "RLK demo_viewer - ca";
    } else if (!strcmp(which, "nav")) {
        static src_nav_t nav;
        static src_t src;
        src_nav_init(&nav);
        src = src_nav_source(&nav);
        cfg.src = &src;
        cfg.title = "RLK demo_viewer - nav";
    } else {
        fprintf(stderr, "usage: %s [world|ca|bsp|nav] [seed]\n", argv[0]);
        return 2;
    }

    return viewer_app_run(&cfg, &a);
}
