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
#include "mask.h"
#include "poi.h"
#include "tilemap.h"

/* Layer 2: terrain algorithms */
#include "slope.h"
#include "drain.h"

/* Worldgen (layers 3-4, wg_ prefix) */
#include "wg_nf.h"
#include "wg_nf_modulator.h"
#include "wg_nf_grid.h"
#include "wg_nf_drain.h"
#include "wg_nf_relief.h"
#include "wg_nf_rivers.h"
#include "wg_nf_poi.h"
#include "wg_ca.h"
#include "wg_bsp.h"

#endif /* RLK_H */
