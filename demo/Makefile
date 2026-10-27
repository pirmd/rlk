# rlk - Examples

DEMO = demo_tilemap_viewer

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
SRC_PXL = demo_tilemap_viewer
DEMO_PXL = $(SRC_PXL)

DEMO = $(DEMO_PXL) $(DEMO_TXT)

all: $(DEMO)

# Auto-build dependencies from sibling projects
$(RLK_DIR)/librlk.a:
	$(MAKE) -C $(RLK_DIR)

$(PXL_DIR)/libpxl.a:
	$(MAKE) -C $(PXL_DIR)

# PXL demos: link libpxl
.for d in $(DEMO)
$(d): $(d).c $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a
	$(CC) $(CFLAGS) $(d).c $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a -o $@ $(LDFLAGS)
.endfor

$(DEMO): $(RLK_HDR) $(PXL_HDR)
lint:
.for d in $(DEMO_PXL) $(DEMO_TXT)
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) $(d).c
.endfor

clean:
	rm -f $(DEMO)

.PHONY: all lint clean
