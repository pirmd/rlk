# rlk - Minimaliste C library for rogue-like or 2D-grid based games.
#
# Phase 0 is header-only (all static inline, no .c, no hidden allocation),
# so there is no librlk.a to build yet. `make test` builds and runs the
# headless unit tests; `make lint` runs strict syntax checks; `make demo`
# builds the optional pxl-based visualization demo (degrades gracefully if
# pxl/SDL2 are unavailable).
CC     ?= cc
CFLAGS ?= -O0 -g -std=c99 -Wall -Wextra

CFLAGS_LINT  = -Wall -Wextra
CFLAGS_LINT += -Wpedantic -Wshadow -Wvla
CFLAGS_LINT += -Wwrite-strings -Wold-style-definition
CFLAGS_LINT += -Wno-unused-function -Wconversion

HDR = rlk.h rl/err.h rl/rng.h rl/noise.h rl/world/tilemap.h

all: test

# Syntax-only strict checks across headers, tests and demo (if present).
lint:
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) $(HDR)
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) -I. -Itest test/*.c test/*.h
	-if [ -d demo ] && [ -f demo/Makefile ]; then \
		$(MAKE) -C demo lint; \
	fi

test:
	$(MAKE) -C test test

demo:
	-if [ -d demo ] && [ -f demo/Makefile ]; then \
		$(MAKE) -C demo; \
	else \
		echo "demo: pxl-based demo not configured (see docs/ARCHITECTURE.md)"; \
	fi

clean:
	-$(MAKE) -C test clean
	-if [ -d demo ] && [ -f demo/Makefile ]; then \
		$(MAKE) -C demo clean; \
	fi

.PHONY: all lint test demo clean
