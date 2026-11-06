# rlk - Minimal 2D Grid/Roguelike Library

LIB = librlk.a

SRC  = drain.c hydro.c
SRC += path.c path_distance.c
SRC += fov.c
SRC += wg.c wg_region.c wg_drain.c wg_hydro.c wg_relief.c wg_rivers.c wg_poi.c
SRC += ca.c bsp.c

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

clean:
	rm -f *.o $(LIB)
	$(MAKE) -C test clean
	$(MAKE) -C demo clean

.PHONY: all lint test demo check clean
