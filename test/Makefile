# rlk - Unit tests

TST  = test_tilemap test_fmath test_geom test_hash test_noise test_err test_wg test_wg_modulator test_wg_grid test_wg_drain test_wg_hydro test_wg_rivers test_wg_relief test_ca test_bsp test_grid test_drain test_hydro test_mask test_slope test_path test_fov test_poi test_wg_poi

RLK_DIR=..

include $(RLK_DIR)/Makefile.inc

CFLAGS  += $(RLK_CFLAGS)
CFLAGS  += -I.

LDFLAGS += $(RLK_LDLIBS)

all: test

test: $(TST)
	@for t in $(TST); do \
		./$${t} >/dev/null || { echo "$${t} FAIL"; exit 1; }; \
		done

.for t in $(TST)
$(t): $(t).c $(RLK_DIR)/librlk.a
	$(CC) $(CFLAGS) $(t).c -o $@ $(LDFLAGS)
.endfor

${TST}: $(RLK_HDR)

lint:
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) *.c

clean:
	@rm -f $(TST)

.PHONY: all test lint clean
