# rlk - Unit tests

TST  = test_err test_hash test_noise test_fmath
TST += test_geom test_grid test_mask test_poi test_tilemap
TST += test_slope test_drain test_hydro
TST += test_path test_path_distance
TST += test_fov
TST += test_wg test_wg_region test_wg_drain test_wg_hydro test_wg_relief test_wg_rivers test_wg_poi
TST += test_ca test_bsp

RLK_DIR=..

include $(RLK_DIR)/Makefile.inc

CFLAGS  += $(RLK_CFLAGS)
LDFLAGS += $(RLK_LDLIBS)

all: $(TST)
.for t in $(TST)
	@./$(t) >/dev/null || { echo "$(t) FAIL"; exit 1; }
.endfor

.for t in $(TST)
$(t): $(t).c $(RLK_DIR)/librlk.a
	$(CC) $(CFLAGS) $(t).c -o $@ $(LDFLAGS)
.endfor

${TST}: $(RLK_HDR)

$(RLK_DIR)/librlk.a:
	$(MAKE) -C $(RLK_DIR)

lint:
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) *.c

clean:
	rm -f $(TST)

.PHONY: all lint clean
