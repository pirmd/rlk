# Map quality stats (stats.h + demo harness)

Lib kernels in `stats.h`: `rlk_stats_entropy`, `rlk_stats_edge_frac`,
`rlk_stats_pair_entropy`, `rlk_stats_moran_i`, `rlk_stats_lzss_bits`,
`rlk_stats_patches`; connectivity via `rlk_mask_components`. For a whole
raster, `rlk_stats_raster()` bundles the stream probes in one call over a
single workspace:

  static rlk_stats_ws_unit_t ws[RLK_STATS_WS_UNITS(W, H, NTYPES)];  /* const-expr size */
  rlk_stats_raster_t r;
  rlk_stats_raster(stream, W, H, NTYPES, &r, ws);

The demo aggregates them with game-specific counts in `tilemap_gen_stats()`
(demo/gen_stats.c, struct in demo/tilemap_gen.h). `demo/test_tilemap_gen.c`
runs the probes per seed:

  make check                        # golden check on 5 seeds (rc != 0 on drift)
  demo/test_tilemap_gen record      # print a new GOLDEN table (intentional changes only)
  demo/test_tilemap_gen report [n]  # multi-seed table (seeds 1..n, default 25):
                                    # min/max/mean, cross-seed diversity, playability proxy

Golden policy: floats with tolerance (entropy/pair/moran 1e-3, fractions/lzss
1e-4), counts exact; re-record only on an intentional change. A determinism
check (same seed twice, tilemap hash compared) runs on every golden seed and
never needs re-record.

## Stats

Computed from the tilemap/elevation, deterministic per seed:

- entropy       Shannon entropy of tile types, bits (composition diversity)
- water/river/land_frac   tile-type coverage fractions
- edge_frac     fraction of adjacent (4-conn) tile pairs with different
                types: spatial heterogeneity ("join count"-like)
- pair_entropy  Shannon entropy of adjacent tile pairs (right/down), bits:
                spatial structure the unigram entropy misses (n-gram proxy)
- n_patches     number of 4-connected same-type patches: fragmentation
- moran_elev    Moran's I of the final elevation field (4-conn): terrain
                clumpiness, ~[-1,1], positive = clumped
- compress_ratio  LZSS bit count / raw bit count of the tile stream (4 KiB
                window, match 3..18; only bits are counted, no output buffer):
                compression complexity, low = regular/repetitive
- n_components / largest_component   walkable connectivity (4-conn flood fill)
- n_sinkholes, n_poi[POI_KIND_COUNT]  content counts

Harness-only (report mode, not in the golden):

- cross-seed mean pairwise Hamming distance between the maps of seeds 1..n:
  diversity of the generator itself (SOTA: mean pairwise NCD between levels)
- poiR% / road — playability proxy: over all pairs of settlement POIs
  (village/tower/hamlet/fort), anchors snapped to the nearest walkable tile,
  paths via rlk_path_find on the walkable mask; poiR% = reachable pairs /
  total, road = mean path length in tiles

## SOTA references

- Withington, Cook, Tokarchuk 2024, "On the Evaluation of Procedural Level
  Generation Systems" (arxiv 2404.18657): taxonomy of PCG evaluation —
  representation-direct features, simulation-based playability, cross-run
  diversity, human studies. We cover the first and third families; the
  playability proxy approximates the second with A* reachability.
- arxiv 2201.10334: objective metrics for procgen levels.
- Summerville et al. 2017 ("Understanding Mario"): tile stats + compression
  of the level as a string as complexity proxy (basis of compress_ratio).

Not covered (deliberate, YAGNI): agent-based playability simulation (MCTS),
human evaluation, NCD with a real compressor (zlib) — LZSS bit count is the
zero-dependency proxy.
