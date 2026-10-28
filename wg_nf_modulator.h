#ifndef RLK_WG_NF_MODULATOR_H
#define RLK_WG_NF_MODULATOR_H

/* layer: 3, domain: worldgen */

/*
 * Optional wg_nf stage: Voronoi modulation that biases
 * and scales the noise fields.
 *
 * Applied as: field[i] = (field[i] + bias[i]) * (1 + gain[i])
 * 0 = neutral for both bias and gain.
 */

#include "wg_nf.h"

#define RLK_WG_NF_MODULATOR_MAX_TYPES 8  /* Max distinct Voronoi cell types */

/* One modulator type: bias + gain for all fields. */
typedef struct {
    float bias[RLK_WG_NF_MAX_FIELDS];  /* additive shift */
    float gain[RLK_WG_NF_MAX_FIELDS];  /* scale = 1 + gain; 0 = neutral */
} rlk_wg_nf_modulator_type_t;

/* Voronoi modulator configuration. Const after construction: zero
   defaults (cell 0->128, blend 0->cell/4) are resolved inside the stage,
   the cfg is never mutated. */
typedef struct {
    rlk_wg_nf_modulator_type_t types[RLK_WG_NF_MODULATOR_MAX_TYPES];
    int ntypes;   /* 1..RLK_WG_NF_MODULATOR_MAX_TYPES */
    float cell;   /* Voronoi cell size in tiles; 0 -> 128, else >= 16 */
    float blend;  /* blend band in tiles; 0 -> cell/4, else <= cell/2 */
    uint64_t salt; /* decorrelates several modulator layers */
} rlk_wg_nf_modulator_cfg_t;

/* Build a stage from a const config. The cfg must outlive the wg. */
rlk_wg_nf_stage_t rlk_wg_nf_modulator(const rlk_wg_nf_modulator_cfg_t *cfg);

#endif /* RLK_WG_NF_MODULATOR_H */
