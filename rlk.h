#ifndef RLK_H
#define RLK_H

/*
 * Umbrella header - includes all public API headers, grouped by base
 * strata and domains (see doc/architecture.md). For finer granularity,
 * include individual headers directly.
 */

/* Base: core (user API). Internal helpers such as fmath.h are included
   transitively by the headers that need them; include directly if wanted. */
#include "err.h"
#include "hash.h"
#include "noise.h"

/* Base: data */
#include "geom.h"
#include "grid.h"
#include "mask.h"
#include "poi.h"
#include "tilemap.h"

/* Domain terrain */
#include "slope.h"
#include "drain.h"
#include "hydro.h"

/* Domain nav */
#include "path.h"

/* Domain vision */
#include "fov.h"

/* Domain worldgen: pipeline engine and stages (wg_ prefix) */
#include "wg.h"
#include "wg_modulator.h"
#include "wg_grid.h"
#include "wg_drain.h"
#include "wg_hydro.h"
#include "wg_relief.h"
#include "wg_rivers.h"
#include "wg_poi.h"

/* Domain worldgen: structure generators */
#include "ca.h"
#include "bsp.h"

#endif /* RLK_H */
