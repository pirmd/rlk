#ifndef RLK_WG_NF_MODULATOR_H
#define RLK_WG_NF_MODULATOR_H


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

/* Voronoi modulator configuration. */
typedef struct {
    rlk_wg_nf_modulator_type_t types[RLK_WG_NF_MODULATOR_MAX_TYPES];
    int ntypes;   /* 1..RLK_WG_NF_MODULATOR_MAX_TYPES */
    float cell;   /* Voronoi cell size in tiles; 0 -> 128, else >= 16 */
    float blend;  /* blend band in tiles; 0 -> cell/4, else <= cell/2 */
    uint64_t salt; /* decorrelates several modulator layers */
} rlk_wg_nf_modulator_t;

/* Stage function (use directly with rlk_wg_nf_add_stage if managing stages by hand). */
void rlk_wg_nf_modulator_stage(rlk_wg_nf_ctx_t *ctx, const void *data);

/* Resolve zero defaults: cell 0->128, blend 0->cell/4. Call once after filling. */
void rlk_wg_nf_modulator_init(rlk_wg_nf_modulator_t *m);

/* Init m, then register it as a stage of wg. m must outlive the wg. */
rlk_err_t rlk_wg_nf_modulator_add(rlk_wg_nf_t *wg, rlk_wg_nf_modulator_t *m);

#endif /* RLK_WG_NF_MODULATOR_H */
