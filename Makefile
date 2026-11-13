# rlk - Minimal 2D Grid/Roguelike Library

LIB = librlk.a

SRC  = drain.c hydro.c
SRC += path.c path_distance.c
SRC += fov.c
SRC += wg.c wg_region.c wg_drain.c wg_hydro.c wg_relief.c wg_rivers.c wg_poi.c
SRC += ca.c bsp.c
SRC += world.c

-include config.mk
CC     ?= cc
CFLAGS ?= -O0 -g
# Mandatory flags: appended to whatever base came from sys.mk, the
# environment or config.mk
CFLAGS += -std=c99 -Wall -Wextra

include Makefile.inc

CFLAGS += $(RLK_CFLAGS)

all: $(LIB)

OBJ = ${SRC:.c=.o}

$(OBJ): $(RLK_HDR)

$(LIB): $(OBJ)
	ar rcs $@ $?

lint:
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) $(SRC) $(RLK_HDR)
	$(MAKE) -C test lint
	$(MAKE) -C demo lint

test: $(LIB)
	$(MAKE) -C test
	$(MAKE) -C demo test

demo: $(LIB)
	$(MAKE) -C demo

check: lint test

# Vendoring snapshot: source tarball to drop into a consumer's vendor/rlk
# (see README "Vendoring").
GIT_COMMIT != git rev-parse --short HEAD 2>/dev/null || echo dev
DIST       = rlk-$(GIT_COMMIT)
DIST_FILES = LICENSE README Makefile Makefile.inc config.mk.example $(SRC) $(RLK_HDR)

dist:
	mkdir -p $(DIST)
	cp $(DIST_FILES) $(DIST)/
	tar -czf $(DIST).tar.gz $(DIST)
	rm -f $(DIST)/*
	rmdir $(DIST)

clean:
	rm -f *.o $(LIB) rlk-*.tar.gz
	$(MAKE) -C test clean
	$(MAKE) -C demo clean

.PHONY: all lint test demo check dist clean
