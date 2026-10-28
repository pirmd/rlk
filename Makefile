# rlk - Minimal 2D Grid/Roguelike Library

LIB = librlk.a

SRC = wg_nf.c wg_nf_modulator.c wg_nf_grid.c wg_nf_drain.c wg_nf_rivers.c wg_nf_relief.c ca.c bsp.c drain.c wg_nf_poi.c path.c fov.c

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
	$(MAKE) -C test test

demo: $(LIB)
	$(MAKE) -C demo

clean:
	rm -f *.o $(LIB)
	$(MAKE) -C test clean
	$(MAKE) -C demo clean

.PHONY: all lint test demo clean
