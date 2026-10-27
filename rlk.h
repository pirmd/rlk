#ifndef RLK_H
#define RLK_H

/*
 * Umbrella header - includes all public API headers.
 * For finer granularity, include individual headers directly.
 */

/* Layer 0: core */
#include "err.h"
#include "hash.h"
#include "fmath.h"
#include "noise.h"
#include "rng.h"

/* Layer 1: base data */
#include "geom.h"
#include "grid.h"
#include "tilemap.h"

/* Worldgen (layers 2-4, wg_ prefix) */
#include "wg_flow.h"
#include "wg_poi.h"
#include "wg_nf.h"
#include "wg_nf_modulator.h"
#include "wg_nf_grid.h"
#include "wg_nf_flow.h"
#include "wg_nf_relief.h"
#include "wg_nf_rivers.h"
#include "wg_nf_poi.h"
#include "wg_ca.h"
#include "wg_bsp.h"

#endif /* RLK_H */
