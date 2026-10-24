# rlk - Unit tests

TST  = test_tilemap test_fmath test_hash test_noise test_rng test_err test_wg_nf test_wg_nf_modulator test_wg_nf_grid test_wg_nf_flow test_wg_ca test_wg_bsp test_wg_grid test_wg_flow

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

clean:
	@rm -f $(TST)

.PHONY: all test clean
