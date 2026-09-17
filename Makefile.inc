# rlk build configuration.
#
# RLK is graphics-backend agnostic. The demo links against PXL, which is
# located through PXL_DIR (defaults to the sibling pxl checkout).
#
# Override any of these from config.mk:
#   PXL_DIR     - path to a pxl checkout (default: ../pxl)
#   PXL_BACKEND - pxl backend to build the demo against (x11 or sdl)
RLK_DIR ?= .

RLK_CFLAGS  = -I$(RLK_DIR)
RLK_LDLIBS  = -L$(RLK_DIR) -lrlk

# Locate PXL. Default to a sibling checkout (../pxl), as rlk is a companion
# to pxl. PXL_DIR may be set explicitly in config.mk.
PXL_DIR     ?= ../pxl
PXL_BACKEND ?= x11

# PXL integration (optional - only used by the demo).
RLK_DEMO_CFLAGS = -I$(PXL_DIR)
RLK_DEMO_LDLIBS = -L$(PXL_DIR) -lpxl

# Resolve the pxl backend flags through pxl's own Makefile.inc. We re-include
# it to inherit its backend link flags (X11_LIBS / SDL_LIBS) so the demo links
# correctly without duplicating pxl's pkg-config logic.
PXL_MAKEFILE_INC = $(PXL_DIR)/Makefile.inc
ifneq ($(wildcard $(PXL_MAKEFILE_INC)),)
RLK_DEMO_PXL_BACKEND = $(PXL_BACKEND)
# pxl Makefile.inc uses bmake conditional syntax; we parse the needed vars
# via a small shell probe instead, to stay GNU-make compatible.
RLK_DEMO_BACKEND_LIBS != case "$(RLK_DEMO_PXL_BACKEND)" in \
  sdl) pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2" ;; \
  *)   pkg-config --libs x11 xext 2>/dev/null || echo "-lX11 -lXext" ;; \
  esac
RLK_DEMO_BACKEND_CFLAGS != case "$(RLK_DEMO_PXL_BACKEND)" in \
  sdl) pkg-config --cflags sdl2 2>/dev/null || echo "" ;; \
  *)   echo "-D_DEFAULT_SOURCE" ;; \
  esac
RLK_DEMO_CFLAGS += $(RLK_DEMO_BACKEND_CFLAGS)
RLK_DEMO_LDLIBS += $(RLK_DEMO_BACKEND_LIBS)
endif

RLK_HDR = $(RLK_DIR)/err.h $(RLK_DIR)/grid.h $(RLK_DIR)/view.h $(RLK_DIR)/rlk.h
