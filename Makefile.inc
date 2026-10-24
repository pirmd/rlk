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

RLK_CFLAGS = -I$(RLK_DIR)
RLK_LDLIBS = -L$(RLK_DIR) -lrlk -lm

RLK_HDR = $(RLK_DIR)/err.h $(RLK_DIR)/hash.h $(RLK_DIR)/fmath.h \
	$(RLK_DIR)/noise.h $(RLK_DIR)/rng.h $(RLK_DIR)/tilemap.h \
	$(RLK_DIR)/rlk.h $(RLK_DIR)/wg_nf.h $(RLK_DIR)/wg_nf_modulator.h \
	$(RLK_DIR)/wg_nf_grid.h $(RLK_DIR)/wg_nf_flow.h \
	$(RLK_DIR)/wg_ca.h $(RLK_DIR)/wg_bsp.h $(RLK_DIR)/wg_grid.h \
	$(RLK_DIR)/wg_flow.h
