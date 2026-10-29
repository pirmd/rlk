# rlk - Examples

RLK_DIR=..
PXL_DIR=../../pxl

-include $(PXL_DIR)/config.mk

include $(RLK_DIR)/Makefile.inc
include $(PXL_DIR)/Makefile.inc

CFLAGS  += $(RLK_CFLAGS)
CFLAGS  += $(PXL_CFLAGS)
CFLAGS  += -I.

LDFLAGS += $(RLK_LDLIBS)
LDFLAGS += $(PXL_LDLIBS)
#
# --- PXL demos (graphical) -----------------------------------------------
SRC_PXL = demo_viewer
DEMO_PXL = $(SRC_PXL)

# --- Text demos (terminal, no PXL) ---------------------------------------
# demo_world reuses the shared tilemap_gen object
SRC_TXT = demo_world
DEMO_TXT = $(SRC_TXT)

DEMO = $(DEMO_PXL) $(DEMO_TXT)

# Shared demo objects, linked into the PXL demos that need them
DEMO_OBJ = tilemap_gen.o

all: $(DEMO)

# Auto-build dependencies from sibling projects
$(RLK_DIR)/librlk.a:
	$(MAKE) -C $(RLK_DIR)

$(PXL_DIR)/libpxl.a:
	$(MAKE) -C $(PXL_DIR)

# PXL demos: link libpxl
.for d in $(DEMO_PXL)
$(d): $(d).c $(DEMO_OBJ) $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a
	$(CC) $(CFLAGS) $(PXL_CFLAGS) $(d).c $(DEMO_OBJ) $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a -o $@ $(RLK_LDLIBS) $(PXL_LDLIBS)
.endfor

tilemap_gen.o: tilemap_gen.c tilemap_gen.h $(RLK_HDR)
	$(CC) $(CFLAGS) -c tilemap_gen.c -o $@

# Text demos: link librlk + libm only (plus the shared tilemap_gen object)
.for d in $(DEMO_TXT)
$(d): $(d).c $(DEMO_OBJ) $(RLK_DIR)/librlk.a
	$(CC) $(CFLAGS) $(d).c $(DEMO_OBJ) -o $@ $(RLK_LDLIBS)
.endfor

$(DEMO_PXL): $(RLK_HDR) $(PXL_HDR)
$(DEMO_TXT): $(RLK_HDR)

lint:
.for d in $(DEMO_PXL) $(DEMO_TXT) tilemap_gen
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) $(d).c
.endfor

clean:
	rm -f $(DEMO) $(DEMO_OBJ)

.PHONY: all lint clean
