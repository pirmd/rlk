/*
 * tilemap_gen - fantasy map generator (see tilemap_gen.h).
 *
 * Thin orchestrator: configures the wg fields, builds the coarse hydrology
 * grids (elevation, drain, lakes, hydro), registers the pipeline stages
 * (gen_stages.c), the zone writers (gen_write.c), runs the fill, then the
 * post passes (gen_post.c). Map probes live in gen_stats.c.
 */

#include <assert.h>

#include "gen_internal.h"

void
tilemap_gen_generate(tilemap_gen_t *gen, uint64_t seed) {
    assert(gen);
    gen->seed = seed;

    rlk_tilemap_init(&gen->tm, TM_W, TM_H, gen->buffer, sizeof(gen->buffer));

    /* --- wg field config (before grid fill: relief_noise seeding needs field seeds) --- */
    rlk_wg_t *wg = &gen->wg;
    wg->seed = seed;
    wg->nstages = 0;
    wg->nsampled = F_NSAMPLED;
    wg->nderived = F_NDERIVED;
    wg->fields[F_CONT]   = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 1, .octaves = 3, .freq = 0.012f, .gain = 0.5f };
    wg->fields[F_HILLS]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 2, .octaves = 4, .freq = 0.050f, .gain = 0.5f };
    wg->fields[F_RIDGE]  = (rlk_wg_field_t){ .kind = RLK_WG_RIDGE, .seed = 3, .octaves = 4, .freq = 0.030f, .gain = 0.5f };
    wg->fields[F_WARP_X] = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 4, .octaves = 2, .freq = 0.025f, .gain = 0.5f };
    wg->fields[F_WARP_Y] = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 5, .octaves = 2, .freq = 0.025f, .gain = 0.5f };
    wg->fields[F_FJORD]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 6, .octaves = 2, .freq = 0.040f, .gain = 0.5f };
    wg->fields[F_TEMP]   = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 7, .octaves = 4, .freq = 0.040f, .gain = 0.5f };
    wg->fields[F_DETAIL] = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 8, .octaves = 2, .freq = 0.050f, .gain = 0.5f };
    wg->fields[F_WIG_X]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 9, .octaves = 2, .freq = 0.100f, .gain = 0.5f };
    wg->fields[F_WIG_Y]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 10, .octaves = 2, .freq = 0.120f, .gain = 0.5f };
    wg->fields[F_CREEK]  = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 11, .octaves = 2, .freq = 0.025f, .gain = 0.5f };
    wg->fields[F_HUM]    = (rlk_wg_field_t){ .kind = RLK_WG_FBM,   .seed = 12, .octaves = 4, .freq = 0.040f, .gain = 0.5f };

    /* --- Provinces: stock Voronoi modulator biasing the sampled fields before
       relief/rivers. Type 0 is neutral; 1..4 are ARID/LUSH/COLD/ISLES.
       cell 64 tiles, blend 16 tiles: macro-zones a few cells wide. */
    gen->prov_cfg = (rlk_wg_modulator_cfg_t){
        .ntypes = 5,
        .cell = 64.0f,
        .blend = 16.0f,
        .salt = 0x9200ULL,
    };
    gen->prov_cfg.types[1].bias[F_HUM]  = -0.14f;  /* ARID: dry + hot   */
    gen->prov_cfg.types[1].bias[F_TEMP] = +0.08f;
    gen->prov_cfg.types[2].bias[F_HUM]  = +0.14f;  /* LUSH: wet         */
    gen->prov_cfg.types[3].bias[F_TEMP] = -0.16f;  /* COLD: cold        */
    gen->prov_cfg.types[4].bias[F_CONT] = -0.10f;  /* ISLES: more sea   */
    gen->prov_mod = rlk_wg_modulator(&gen->prov_cfg);

    /* --- Coarse elevation grid: seed noise like wg, fill with relief terrain ---
       The drain grid must see the SAME terrain the relief stage displays,
       otherwise rivers follow a different landscape than what is shown. */
    for (int i = 0; i < F_NSAMPLED; i++) {
        uint64_t s = rlk_hash_sub(seed, 0xF1E1D000ULL + (uint64_t)i);
        rlk_noise_seed(&gen->relief_noise[i], rlk_hash_sub(s, wg->fields[i].seed));
    }
    /* Light geology noise (demo-owned, not wg fields) */
    for (int i = 0; i < 3; i++)
        rlk_noise_seed(&gen->geo_noise[i], rlk_hash_sub(seed, 0x6E0600ULL + (uint64_t)i));
    gen_relief_ctx_t elev_ctx = {
        .gen = gen,
        .wg = wg,
        .noise = gen->relief_noise,
        .warp = 8.0f,
        .hills_amp = 0.30f,
        .mountain_lo = 0.60f,
        .mountain_hi = 0.80f,
        .mountain_amp = 0.45f,
    };
    gen->elev_grid = (rlk_grid_t){ .lattice = { .w = GRID_W, .h = GRID_H, .cellsize = GRID_CELL,
                                    .origin_x = GRID_ORIGIN_X, .origin_y = GRID_ORIGIN_Y }, .v = gen->elev_buf };
    for (int j = 0; j < GRID_H; j++)
        for (int i = 0; i < GRID_W; i++) {
            float cx, cy;
            rlk_grid_center(&gen->elev_grid, i, j, &cx, &cy);
            gen->elev_buf[(size_t)j * (size_t)GRID_W + (size_t)i] = gen_relief_elev_fn(cx, cy, &elev_ctx);
        }

    /* --- Drain: depression filling + D8 drainage + accumulation --- */
    gen->drain = (rlk_drain_t){ .lattice = gen->elev_grid.lattice,
                              .filled = gen->drain_filled, .dir = gen->drain_dir,
                              .acc = gen->drain_acc, .order = gen->drain_order };
    rlk_err_t derr = rlk_drain_build(&gen->drain, &gen->elev_grid, SEA_LEVEL, gen->drain_scratch);
    assert(derr == RLK_OK);

    /* --- Lake grid: filled depression depths, filtered by outlet accumulation ---
       Lakes whose outflow drains fewer than RIVER_ACC_MIN cells are dropped:
       they would sit with no visible outlet. */
    gen->lake_grid = (rlk_grid_t){ .lattice = { .w = GRID_W, .h = GRID_H, .cellsize = GRID_CELL,
                                    .origin_x = GRID_ORIGIN_X, .origin_y = GRID_ORIGIN_Y }, .v = gen->lake_buf };
    for (int j = 0; j < GRID_H; j++)
        for (int i = 0; i < GRID_W; i++)
            gen->lake_buf[(size_t)j * (size_t)GRID_W + (size_t)i] =
                rlk_drain_lake_depth(&gen->drain, &gen->elev_grid, SEA_LEVEL, i, j);
    /* Filter: drop lake cells whose outlet accumulation < RIVER_ACC_MIN */
    for (int c = 0; c < GRID_W * GRID_H; c++) {
        if (gen->lake_buf[c] <= 0.0f) continue;
        int cur = c;
        for (;;) {
            if (gen->drain.dir[cur] == RLK_DRAIN_OUTLET) break;
            int dx, dy;
            rlk_drain_step(gen->drain.dir[cur], &dx, &dy);
            const int nx = (cur % GRID_W) + dx, ny = (cur / GRID_W) + dy;
            if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) break;
            const int nc = ny * GRID_W + nx;
            if (gen->lake_buf[nc] <= 0.0f) break;
            cur = nc;
        }
        if (gen->drain.acc[cur] < RIVER_ACC_MIN) gen->lake_buf[c] = 0.0f;
    }

    /* --- Hydro: derived fields on the drain grid (slope, Strahler, speed) --- */
    gen->hydro = (rlk_hydro_t){ .lattice = gen->drain.lattice,
                                .slope = gen->hydro_slope,
                                .strahler = gen->hydro_strahler,
                                .speed = gen->hydro_speed };
    rlk_hydro_build(&gen->hydro, &gen->drain, RIVER_ACC_MIN, gen->hydro_scratch);

    /* --- wg stages: provinces + drain + hydro + rivers + strahler + relief + POI --- */

    /* Stage 0: provinces (F_HUM offset decorrelation + Voronoi modulator).
       Reads/writes all sampled fields (the modulator part). */
    rlk_wg_add(wg, (rlk_wg_stage_t){ .fn = gen_province_stage, .data = gen,
                                      .reads = { .sampled = RLK_WG_SLOTS_ALL },
                                      .writes = { .sampled = RLK_WG_SLOTS_ALL } });

    /* Stage 1: wg_drain — inject accumulation + lake depth */
    gen->drain_stage = (rlk_wg_drain_cfg_t){
        .drain = &gen->drain,
        .elev = &gen->elev_grid,
        .sea = SEA_LEVEL,
        .slot_acc  = RLK_WG_DRAIN_NONE,   /* acc read straight from the drain grid */
        .slot_dir  = RLK_WG_DRAIN_NONE,
        .slot_lake = D_LAKE,
        .acc_scale = 1.0f,
    };
    rlk_wg_add(wg, rlk_wg_drain(&gen->drain_stage));

    /* Stage 2: wg_hydro — inject slope / Strahler order / speed */
    gen->hydro_stage = (rlk_wg_hydro_cfg_t){
        .hydro = &gen->hydro,
        .slot_slope = D_SLOPE,
        .slot_strahler = D_STRAHLER,
        .slot_speed = D_SPEED,
    };
    rlk_wg_add(wg, rlk_wg_hydro(&gen->hydro_stage));

    /* Stage 3: wg_rivers — project drainage into per-tile river masks.
       Channel width follows the drain SDF (~log2(acc), capped at cellsize);
       mask fades 1 tile past the bank, valley reach = 2 * cellsize.
       Meanders from F_WIG_X/Y, creeks from F_CREEK, lakes from the filtered grid. */
    gen->rivers_stage = (rlk_wg_rivers_cfg_t){
        .drain = &gen->drain,
        .elev = &gen->elev_grid,
        .sea = SEA_LEVEL,
        .acc_min = RIVER_ACC_MIN,
        .bank_fade = 1.0f,
        .valley_mult = 2.0f,
        .carve = 0.03f,
        .slot_mask = D_RIVER_MASK,
        .slot_carve = D_RIVER_CARVE,
        .lake_depth = &gen->lake_grid,
        .lake_min_depth = LAKE_MIN_DEPTH,
        .f_wig_x = F_WIG_X,
        .f_wig_y = F_WIG_Y,
        .wig_tiles = 3.0f,
        .f_creek = F_CREEK,
        .creek_width = 1.0f,
        .creek_carve = 0.02f,
    };
    rlk_wg_add(wg, rlk_wg_rivers(&gen->rivers_stage));

    /* Stage 4: Strahler width — narrow headwaters, wide big rivers */
    rlk_wg_add(wg, (rlk_wg_stage_t){ .fn = gen_strahler_stage, .data = gen,
                                      .reads = { .derived = RLK_WG_SLOT(D_RIVER_MASK) | RLK_WG_SLOT(D_STRAHLER) },
                                      .writes = { .derived = RLK_WG_SLOT(D_RIVER_MASK) } });

    /* Stage 5: wg_relief — standardised elevation (domain warp, fjords, lapse) */
    gen->relief_stage = (rlk_wg_relief_cfg_t){
        .f_cont = F_CONT,
        .f_hills = F_HILLS,
        .f_ridge = F_RIDGE,
        .f_warp_x = F_WARP_X,
        .f_warp_y = F_WARP_Y,
        .f_fjord = F_FJORD,
        .f_temp = F_TEMP,
        .f_river = RLK_WG_RELIEF_NONE,  /* rivers come from wg_rivers */
        .slot_elev = D_ELEV,
        .slot_river = RLK_WG_RELIEF_NONE,
        .river_carve_field = D_RIVER_CARVE,  /* read carve from wg_rivers */
        .sea = SEA_LEVEL,
        .warp = 8.0f,
        .hills_amp = 0.30f,
        .mountain_lo = 0.60f,
        .mountain_hi = 0.80f,
        .mountain_amp = 0.45f,
        .lapse = 0.25f,
        .river_width_tiles = 2.0f,
        .river_carve = 0.03f,
        .relief_span = 0.40f,
        .fjord_dir = { 0.8f, 0.6f },
        .fjord_stretch = 0.25f,
        .fjord_width = 0.12f,
        .fjord_depth = 0.50f,
        .fjord_steep = 3.0f,
        .fjord_reach = 0.15f,
        .fjord_cold0 = 0.45f,
        .fjord_cold1 = 0.65f,
    };
    gen->relief_mod = rlk_wg_relief(&gen->relief_stage);
    /* Wrapper around the stock relief stage, adding the rain shadow on F_HUM.
       data must be the tilemap_gen_t (gen_relief_rain_stage reads gen->relief_mod),
       not the relief cfg that the stock stage carries. */
    rlk_wg_stage_t rain = gen->relief_mod;
    rain.fn = gen_relief_rain_stage;
    rain.data = gen;
    rain.reads.sampled |= RLK_WG_SLOT(F_HUM);
    rain.writes.sampled |= RLK_WG_SLOT(F_HUM);
    rlk_wg_add(wg, rain);

    /* Stage 6: geology — structural bias on the final elevation + lithology */
    rlk_wg_add(wg, (rlk_wg_stage_t){ .fn = gen_geology_stage, .data = gen,
                                      .reads = { .derived = RLK_WG_SLOT(D_ELEV) },
                                      .writes = { .derived = RLK_WG_SLOT(D_ELEV) } });

    /* Stage 7-10: POI detectors */
    gen->pois = (rlk_poi_list_t){ gen->poi_buf, 0, 256 };

    /* Cave: on hills/mountains, high detail noise */
    gen->poi_cave_cfg = (rlk_wg_poi_cfg_t){
        .list = &gen->pois,
        .type = POI_CAVE,
        .salt = 0xCAFEULL,
        .f_sampled = F_DETAIL,
        .threshold = 0.70f,
        .density = 0.003f,
        .size = 24,
        .grid = &gen->elev_grid,
        .grid_min = 0.40f,
        .grid_max = 0.85f,
        .min_separation = 24,
    };
    rlk_wg_add(wg, rlk_wg_poi(&gen->poi_cave_cfg));

    /* Dungeon: near rivers */
    gen->poi_dungeon_cfg = (rlk_wg_poi_cfg_t){
        .list = &gen->pois,
        .type = POI_DUNGEON,
        .salt = 0xD0D0ULL,
        .f_sampled = F_DETAIL,
        .threshold = 0.50f,
        .density = 0.002f,
        .size = 32,
        .drain = &gen->drain,
        .min_acc = 30.0f,
        .min_separation = 32,
    };
    rlk_wg_add(wg, rlk_wg_poi(&gen->poi_dungeon_cfg));

    /* Village: warm lowland plains only (temperature + altitude constraints) */
    gen->poi_village_cfg = (rlk_wg_poi_cfg_t){
        .list = &gen->pois,
        .type = POI_VILLAGE,
        .salt = 0xA11CEULL,
        .f_sampled = F_TEMP,
        .threshold = 0.45f,
        .density = 0.006f,
        .size = 16,
        .grid = &gen->elev_grid,
        .grid_min = 0.35f,
        .grid_max = 0.50f,
        .min_separation = 16,
    };
    rlk_wg_add(wg, rlk_wg_poi(&gen->poi_village_cfg));

    /* Tower: on hills and peaks */
    gen->poi_tower_cfg = (rlk_wg_poi_cfg_t){
        .list = &gen->pois,
        .type = POI_TOWER,
        .salt = 0x70E1ULL,
        .f_sampled = F_DETAIL,
        .threshold = 0.60f,
        .density = 0.002f,
        .size = 8,
        .grid = &gen->elev_grid,
        .grid_min = 0.70f,
        .grid_max = 1.0f,
        .min_separation = 8,
    };
    rlk_wg_add(wg, rlk_wg_poi(&gen->poi_tower_cfg));

    /* Stage 11: terrain-constrained POI kinds (zone/slope/river/geology) */
    rlk_wg_add(wg, (rlk_wg_stage_t){ .fn = gen_poi_terrain_stage, .data = gen,
                                      .reads = { .sampled = RLK_WG_SLOT(F_TEMP) | RLK_WG_SLOT(F_HUM),
                                                 .derived = RLK_WG_SLOT(D_ELEV) | RLK_WG_SLOT(D_RIVER_MASK) | RLK_WG_SLOT(D_SLOPE) } });

    /* Axes: elevation (relief output), temperature (lapse-cooled), humidity */
    wg->naxes = 3;
    wg->axes[0] = (rlk_wg_axis_t){ .field = F_ELEV,  .jitter_field = F_DETAIL, .jitter_amp = 0.03f };
    wg->axes[1] = (rlk_wg_axis_t){ .field = F_TEMP, .jitter_field = F_DETAIL, .jitter_amp = 0.03f };
    wg->axes[2] = (rlk_wg_axis_t){ .field = F_HUM,  .jitter_field = F_DETAIL, .jitter_amp = 0.03f };

    /* Zones: elevation slices on axis 0; the lowland band splits into the 6
       Köppen biomes on (temp, hum). Unbound axes use +/- RLK_WG_OPEN. */
    wg->nzones = Z_COUNT;

    /* Each zone writes through gen_capture_write, dispatched to the zone
       writer table (gen_write.c). */
    gen_setup_captures(gen);

#define Z_OPEN_LO (-RLK_WG_OPEN)
#define Z_OPEN_HI ( RLK_WG_OPEN)
    wg->zones[Z_DEEP]     = (rlk_wg_zone_t){ .lo = {0.0f,        Z_OPEN_LO, Z_OPEN_LO}, .hi = {ELEV_DEEP,    Z_OPEN_HI, Z_OPEN_HI}, .write = gen_capture_write, .data = &gen->cap[Z_DEEP] };
    wg->zones[Z_SHALLOW]  = (rlk_wg_zone_t){ .lo = {ELEV_DEEP,   Z_OPEN_LO, Z_OPEN_LO}, .hi = {ELEV_SHALLOW, Z_OPEN_HI, Z_OPEN_HI}, .write = gen_capture_write, .data = &gen->cap[Z_SHALLOW] };
    wg->zones[Z_BEACH]    = (rlk_wg_zone_t){ .lo = {ELEV_SHALLOW, Z_OPEN_LO, Z_OPEN_LO}, .hi = {ELEV_BEACH,  Z_OPEN_HI, Z_OPEN_HI}, .write = gen_capture_write, .data = &gen->cap[Z_BEACH] };
    /* Lowland: 6 biomes on (temp, hum) */
    wg->zones[Z_DESERT]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   TEMP_HOT,  0.0f},     .hi = {ELEV_LOWLAND, Z_OPEN_HI, HUM_DRY},   .write = gen_capture_write, .data = &gen->cap[Z_DESERT] };
    wg->zones[Z_FOREST]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   TEMP_HOT,  HUM_DRY},  .hi = {ELEV_LOWLAND, Z_OPEN_HI, Z_OPEN_HI}, .write = gen_capture_write, .data = &gen->cap[Z_FOREST] };
    wg->zones[Z_STEPPE]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   TEMP_COLD, 0.0f},     .hi = {ELEV_LOWLAND, TEMP_HOT,  HUM_DRY},   .write = gen_capture_write, .data = &gen->cap[Z_STEPPE] };
    wg->zones[Z_PLAINS]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   TEMP_COLD, HUM_DRY},  .hi = {ELEV_LOWLAND, TEMP_HOT,  Z_OPEN_HI}, .write = gen_capture_write, .data = &gen->cap[Z_PLAINS] };
    wg->zones[Z_TUNDRA]   = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   Z_OPEN_LO, 0.0f},     .hi = {ELEV_LOWLAND, TEMP_COLD, HUM_DRY},   .write = gen_capture_write, .data = &gen->cap[Z_TUNDRA] };
    wg->zones[Z_TAIGA]    = (rlk_wg_zone_t){ .lo = {ELEV_BEACH,   Z_OPEN_LO, HUM_DRY},  .hi = {ELEV_LOWLAND, TEMP_COLD, Z_OPEN_HI}, .write = gen_capture_write, .data = &gen->cap[Z_TAIGA] };
    wg->zones[Z_MOUNTAIN] = (rlk_wg_zone_t){ .lo = {ELEV_LOWLAND, Z_OPEN_LO, Z_OPEN_LO}, .hi = {ELEV_MOUNTAIN, Z_OPEN_HI, Z_OPEN_HI}, .write = gen_capture_write, .data = &gen->cap[Z_MOUNTAIN] };
    wg->zones[Z_SNOW]     = (rlk_wg_zone_t){ .lo = {ELEV_MOUNTAIN, Z_OPEN_LO, Z_OPEN_LO}, .hi = {1.0f,         Z_OPEN_HI, Z_OPEN_HI}, .write = gen_capture_write, .data = &gen->cap[Z_SNOW] };
#undef Z_OPEN_LO
#undef Z_OPEN_HI

    wg->depth_scale = 0.15f;
    wg->transition_width = 0.10f;

    assert(rlk_wg_init(wg) == RLK_OK);
    rlk_wg_fill(wg, 0, 0, &gen->tm);

    /* Multi-tile content inside each POI bbox (first POI wins on overlap) */
    gen_poi_content_pass(gen);

    /* Cosmetic: karst dolines, river-mouth deltas, oxbow ponds, karst caves */
    gen_cosmetic_pass(gen);
}
