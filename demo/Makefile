# rlk - Examples
#
# Flat layout, like the lib itself. Prefixes mark what is generic vs demo-only:
#   demo_*     interactive demo binaries (PXL viewer, terminal world, GIS export)
#   test_*     harnesses run by `check` (golden non-regression)
#   tilemap_gen + gen_*    fantasy map generator package (pure demo)
#   src_*      viewer demo sources (pure demo scenarios)
#   gis_export.h + source/viewer/viewer_app/widget/font_6x10.h
#              generic reusable blocks (GIS export helpers, PXL viewer harness) —
#              candidates for future tools (e.g. pirr map report / HUD probes);
#              promote only with a real second consumer (YAGNI)

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
# --- PXL demo (graphical) --------------------------------------------------
SRC_PXL = demo_viewer
DEMO_PXL = $(SRC_PXL)

# --- Text binaries (terminal, no PXL) --------------------------------------
# all reuse the shared tilemap_gen objects
# demo_* = interactive demos, test_* = runnable harnesses (run by `check`)
SRC_TXT = demo_world demo_gis test_tilemap_gen
DEMO_TXT = $(SRC_TXT)
TST_TXT  = test_tilemap_gen

DEMO = $(DEMO_PXL) $(DEMO_TXT)

# Shared objects of the tilemap_gen package, linked into the demos
GEN_SRC = tilemap_gen gen_stages gen_write gen_post gen_stats
GEN_OBJ = $(GEN_SRC:=.o)

all: $(DEMO)

# Non-regression: run the demo-side test harnesses, rc != 0 on failure.
check: $(TST_TXT)
	@for t in $(TST_TXT); do \
		./$${t} || { echo "$${t} FAIL"; exit 1; }; \
	done

# Auto-build dependencies from sibling projects
$(RLK_DIR)/librlk.a:
	$(MAKE) -C $(RLK_DIR)

$(PXL_DIR)/libpxl.a:
	$(MAKE) -C $(PXL_DIR)

# PXL demos: link libpxl
.for d in $(DEMO_PXL)
$(d): $(d).c $(GEN_OBJ) $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a
	$(CC) $(CFLAGS) $(PXL_CFLAGS) $(d).c $(GEN_OBJ) $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a -o $@ $(RLK_LDLIBS) $(PXL_LDLIBS)
.endfor

.for o in $(GEN_SRC)
$(o).o: $(o).c gen_internal.h tilemap_gen.h $(RLK_HDR)
	$(CC) $(CFLAGS) -c $(o).c -o $@
.endfor

# Text demos: link librlk + libm only (plus the shared tilemap_gen objects)
.for d in $(DEMO_TXT)
$(d): $(d).c $(GEN_OBJ) $(RLK_DIR)/librlk.a
	$(CC) $(CFLAGS) $(d).c $(GEN_OBJ) -o $@ $(RLK_LDLIBS)
.endfor

$(DEMO_PXL): $(RLK_HDR) $(PXL_HDR) source.h viewer.h viewer_app.h widget.h \
	font_6x10.h src_world.h src_dungeon.h src_nav.h tilemap_gen.h
demo_world: $(RLK_HDR) tilemap_gen.h gen_internal.h
demo_gis: $(RLK_HDR) tilemap_gen.h gen_internal.h gis_export.h
test_tilemap_gen: $(RLK_HDR) tilemap_gen.h gen_internal.h

lint:
.for d in $(DEMO_PXL) $(DEMO_TXT) $(GEN_SRC)
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) $(d).c
.endfor

clean:
	rm -f $(DEMO) $(GEN_OBJ)

.PHONY: all lint check clean
