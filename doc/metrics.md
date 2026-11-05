# Map quality metrics (demo harness)

Where: `tilemap_gen_stats()` (demo/gen_stats.c) fills `tilemap_gen_stats_t`
(demo/tilemap_gen.h). The harness `demo/test_tilemap_gen.c` runs them:

  make check                        # golden check on 5 seeds (rc != 0 on drift)
  demo/test_tilemap_gen record      # print a new GOLDEN table (intentional changes only)
  demo/test_tilemap_gen report [n]  # multi-seed table (seeds 1..n, default 25) with
                                    # min/max/mean, cross-seed diversity, playability

Golden policy: floats with tolerance (entropy/pair/moran 1e-3, fractions/lzss
1e-4), integer counts exact. Re-record only on an intentional change of the
generation or of the stats output. A determinism check (same seed generated
twice, tilemap hash compared) runs on every golden seed and never needs
re-record.

## Metrics

Representation-direct (computed from the tilemap/elevation, deterministic
per seed):

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
- poiR% / road  playability proxy: over all pairs of surface settlement POIs
  (village/tower/hamlet/fort), anchors snapped to the nearest walkable tile,
  rlk_path_find on the walkable mask; poiR% = reachable pairs / total,
  road = mean path length in tiles over reachable pairs

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
