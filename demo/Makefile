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

all: $(DEMO)

.for d in $(DEMO)
$(d): $(d).c $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a
	$(CC) $(CFLAGS) $(d).c $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a -o $@ $(LDFLAGS)
.endfor

$(DEMO): $(RLK_HDR) $(PXL_HDR)

clean:
	rm -f $(DEMO)

.PHONY: all clean
