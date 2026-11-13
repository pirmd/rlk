# Requires bmake (NetBSD make).

RLK_DIR ?= .

# Determinism contract: no -ffast-math, no implicit FMA.
RLK_CFLAGS = -I$(RLK_DIR) -ffp-contract=off
RLK_LDLIBS = -L$(RLK_DIR) -lrlk -lm

# Shared lint flags: root lib, tests and demos all use the same set.
# Developer-local extras: CFLAGS_LINT_LOCAL (see config.mk.example).
CFLAGS_LINT  = -Wall -Wextra
CFLAGS_LINT += -Wpedantic -Wshadow -Wvla
CFLAGS_LINT += -Wwrite-strings -Wold-style-definition
CFLAGS_LINT += -Wconversion
CFLAGS_LINT += $(CFLAGS_LINT_LOCAL)

RLK_HDR = $(RLK_DIR)/err.h $(RLK_DIR)/hash.h $(RLK_DIR)/fmath.h \
	$(RLK_DIR)/noise.h $(RLK_DIR)/geom.h \
	$(RLK_DIR)/grid.h $(RLK_DIR)/mask.h $(RLK_DIR)/poi.h \
	$(RLK_DIR)/scatter.h \
	$(RLK_DIR)/tilemap.h $(RLK_DIR)/entity.h $(RLK_DIR)/slope.h $(RLK_DIR)/drain.h \
	$(RLK_DIR)/hydro.h $(RLK_DIR)/path.h $(RLK_DIR)/path_distance.h $(RLK_DIR)/fov.h \
	$(RLK_DIR)/rlk.h \
	$(RLK_DIR)/wg.h $(RLK_DIR)/wg_region.h \
	$(RLK_DIR)/wg_drain.h $(RLK_DIR)/wg_hydro.h \
	$(RLK_DIR)/wg_rivers.h $(RLK_DIR)/wg_relief.h \
	$(RLK_DIR)/wg_poi.h $(RLK_DIR)/ca.h $(RLK_DIR)/bsp.h $(RLK_DIR)/world.h
