# rlk - pxl visualization demo (Phase 0b).
#
# This target is OPTIONAL: it requires pxl + a backend (SDL2 or X11).
# It is NOT part of `make test`. If pxl is unavailable, build/lint degrade
# gracefully. PXL_DIR points at a checked-out pxl source tree.
#
# Backend flags come from the environment or the caller (pxl's Makefile.inc is
# bmake-specific, so we do not include it here). Typical usage:
#   export PXL_BACKEND=sdl
#   make -C demo PXL_DIR=/path/to/pxl
#
# Provide PXL_CFLAGS / PXL_LDLIBS explicitly if auto-detection is unwanted:
#   make -C demo PXL_DIR=/path/to/pxl \
#       PXL_CFLAGS="$(sdl2-config --cflags)" PXL_LDLIBS="-L/path/to/pxl -lpxl $(sdl2-config --libs)"
PXL_DIR ?= ../pxl
RLK_DIR = ..
PXL_BACKEND ?= x11

CC     ?= cc
CFLAGS ?= -O0 -g -std=c99 -Wall -Wextra
CFLAGS += -I$(RLK_DIR) -I$(PXL_DIR)

# Backend flags: use caller-provided PXL_CFLAGS/PXL_LDLIBS if set, else try
# pkg-config for the selected backend. Falls back to common defaults.
PXL_CFLAGS ?= $(shell pkg-config --cflags $(PXL_BACKEND_LIBS_$(PXL_BACKEND)) 2>/dev/null)
PXL_LDLIBS ?= $(shell pkg-config --libs   $(PXL_BACKEND_LIBS_$(PXL_BACKEND)) 2>/dev/null) -L$(PXL_DIR) -lpxl
PXL_BACKEND_LIBS_x11 = x11 xext
PXL_BACKEND_LIBS_sdl = sdl2

CFLAGS  += $(PXL_CFLAGS)
LDFLAGS += $(PXL_LDLIBS)

DEMO = demo_tilemap
PXL_LIB = $(PXL_DIR)/libpxl.a

all: $(DEMO)

# Require the pxl source tree and its lib before attempting the demo.
$(DEMO): demo_tilemap.c $(PXL_LIB)
	@if [ ! -f $(PXL_LIB) ]; then \
		echo "demo: pxl library not found ($(PXL_LIB)); build pxl first"; exit 1; \
	fi
	$(CC) $(CFLAGS) demo_tilemap.c -o $(DEMO) $(LDFLAGS)

# Build pxl (bmake). If bmake is unavailable, the caller must build pxl itself.
$(PXL_LIB):
	@echo "demo: building pxl ($(PXL_DIR), backend=$(PXL_BACKEND))..."
	@$(MAKE) -C $(PXL_DIR) PXL_BACKEND=$(PXL_BACKEND) || \
	 { echo "demo: could not build pxl (it uses bmake); build it manually"; exit 1; }

# Strict syntax-only check (no link), used by the top-level `make lint`.
# Requires pxl headers to be present (PXL_DIR); skips gracefully otherwise.
lint:
	@if [ -f $(PXL_DIR)/pxl.h ]; then \
		$(CC) -Werror -fsyntax-only $(CFLAGS) \
		    -Wall -Wextra -Wpedantic -Wshadow -Wwrite-strings \
		    -Wno-unused-function demo_tilemap.c; \
	else \
		echo "demo lint: pxl headers not found ($(PXL_DIR)), skipping"; \
	fi

clean:
	rm -f $(DEMO)

.PHONY: all lint clean
