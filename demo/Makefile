# rlk - Examples

DEMO = demo_world demo_gis
DEMO_PXL =
TST  = test_tilemap_gen

RLK_DIR=..

include $(RLK_DIR)/Makefile.inc

CFLAGS  += $(RLK_CFLAGS) -I.
LDFLAGS += $(RLK_LDLIBS)

# PXL viewer is optional: set PXL_DIR (config.mk, env or command line) to a
# pxl checkout. Its Makefile.inc is included hard (a set PXL_DIR must be
# valid) and provides the flags of PXL_BACKEND (x11 by default).
.if !empty(PXL_DIR)
DEMO_PXL = demo_viewer
include $(PXL_DIR)/Makefile.inc
CFLAGS  += $(PXL_CFLAGS)
LDFLAGS += $(PXL_LDLIBS)
.endif

GEN_SRC = tilemap_gen.c gen_stages.c gen_write.c gen_post.c gen_stats.c
GEN_OBJ = ${GEN_SRC:.c=.o}

all: $(DEMO) $(DEMO_PXL)

$(GEN_OBJ): gen_internal.h tilemap_gen.h $(RLK_HDR)

$(RLK_DIR)/librlk.a:
	$(MAKE) -C $(RLK_DIR)

.for d in $(DEMO) $(TST)
$(d): $(d).c $(GEN_OBJ) $(RLK_DIR)/librlk.a
	$(CC) $(CFLAGS) $(d).c $(GEN_OBJ) -o $@ $(LDFLAGS)
.endfor

demo_world: $(RLK_HDR) tilemap_gen.h gen_internal.h

demo_gis: $(RLK_HDR) tilemap_gen.h gen_internal.h gis_export.h

.if !empty(PXL_DIR)
$(PXL_DIR)/libpxl.a:
	$(MAKE) -C $(PXL_DIR)

.for d in $(DEMO_PXL)
$(d): $(d).c $(GEN_OBJ) $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a
	$(CC) $(CFLAGS) $(d).c $(GEN_OBJ) $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a -o $@ $(LDFLAGS)
.endfor

$(DEMO_PXL): $(RLK_HDR) $(PXL_HDR) source.h viewer.h viewer_app.h widget.h \
	font_6x10.h src_world.h src_dungeon.h tilemap_gen.h
.endif

test_tilemap_gen: $(RLK_HDR) tilemap_gen.h gen_internal.h

lint:
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) $(GEN_SRC) \
		$(DEMO:=.c) $(TST:=.c) $(DEMO_PXL:=.c)

test: $(TST)
.for t in $(TST)
	@./$(t) || { echo "$(t) FAIL"; exit 1; }
.endfor

clean:
	rm -f $(DEMO) $(DEMO_PXL) $(TST) *.o

.PHONY: all lint test clean
