# Requires bmake (NetBSD make). Native on OpenBSD, `apt install bmake` on Linux.

RLK_DIR ?= .

# User configuration: config.mk owns CC/CFLAGS when present. The explicit
# default is required because OpenBSD sys.mk predefines CFLAGS, which would
# defeat a `CFLAGS ?=` default.
.if exists($(RLK_DIR)/config.mk)
.include "$(RLK_DIR)/config.mk"
.else
CC     = cc
CFLAGS = -O0 -g -std=c99 -Wall -Wextra
.endif

# Determinism contract: no -ffast-math, no implicit FMA.
RLK_CFLAGS = -I$(RLK_DIR) -ffp-contract=off
RLK_LDLIBS = -L$(RLK_DIR) -lrlk -lm

# Shared lint flags: root lib, tests and demos all use the same set
CFLAGS_LINT  = -Wall -Wextra
CFLAGS_LINT += -Wpedantic -Wshadow -Wvla
CFLAGS_LINT += -Wwrite-strings -Wold-style-definition
CFLAGS_LINT += -Wno-unused-function
CFLAGS_LINT += -Wconversion

RLK_HDR = $(RLK_DIR)/err.h $(RLK_DIR)/hash.h $(RLK_DIR)/fmath.h \
	$(RLK_DIR)/noise.h $(RLK_DIR)/rng.h $(RLK_DIR)/geom.h \
	$(RLK_DIR)/grid.h $(RLK_DIR)/tilemap.h $(RLK_DIR)/rlk.h \
	$(RLK_DIR)/wg_flow.h $(RLK_DIR)/wg_poi.h \
	$(RLK_DIR)/wg_nf.h $(RLK_DIR)/wg_nf_modulator.h \
	$(RLK_DIR)/wg_nf_grid.h $(RLK_DIR)/wg_nf_flow.h \
	$(RLK_DIR)/wg_nf_rivers.h $(RLK_DIR)/wg_nf_relief.h \
	$(RLK_DIR)/wg_nf_poi.h $(RLK_DIR)/wg_ca.h $(RLK_DIR)/wg_bsp.h
