# rlk - Unit tests (headless, no pxl).
#
# Phase 0 is header-only, so each test compiles directly against the rlk
# headers; no librlk.a link step is needed.
RLK_DIR = ..
CC     ?= cc
CFLAGS ?= -O0 -g -std=c99 -Wall -Wextra
CFLAGS += -I$(RLK_DIR) -I.

TST = test_rng test_noise test_tilemap test_region test_view

all: test

test: $(TST)
	@for t in $(TST); do \
		./$$t >/dev/null || { echo "$$t FAIL"; exit 1; }; \
		echo "$$t ok"; \
	done

test_rng: test_rng.c test.h $(RLK_DIR)/rlk.h
	$(CC) $(CFLAGS) test_rng.c -o test_rng

test_noise: test_noise.c test.h $(RLK_DIR)/rlk.h
	$(CC) $(CFLAGS) test_noise.c -o test_noise

test_tilemap: test_tilemap.c test.h $(RLK_DIR)/rlk.h
	$(CC) $(CFLAGS) test_tilemap.c -o test_tilemap

test_region: test_region.c test.h $(RLK_DIR)/rlk.h
	$(CC) $(CFLAGS) test_region.c -o test_region

test_view: test_view.c test.h $(RLK_DIR)/rlk.h
	$(CC) $(CFLAGS) test_view.c -o test_view

clean:
	@rm -f $(TST)

.PHONY: all test clean
