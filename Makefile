# rlk - Minimal 2D Grid/Roguelike Library

LIB = librlk.a

SRC = wg_nf.c wg_nf_modulator.c wg_nf_grid.c wg_nf_flow.c wg_nf_rivers.c wg_nf_relief.c wg_ca.c wg_bsp.c wg_grid.c wg_flow.c wg_poi.c wg_nf_poi.c

include Makefile.inc

CFLAGS += $(RLK_CFLAGS)

CFLAGS_LINT  = -Wall -Wextra
CFLAGS_LINT += -Wpedantic -Wshadow -Wvla
CFLAGS_LINT += -Wwrite-strings -Wold-style-definition
CFLAGS_LINT += -Wno-unused-function
CFLAGS_LINT += -Wconversion

all: $(LIB)

OBJ = ${SRC:.c=.o}

$(LIB): $(OBJ)
	ar rcs $@ $?

lint:
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) $(SRC) $(HDR)
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) test/*.c

test: $(LIB)
	$(MAKE) -C test test

demo: $(LIB)
	$(MAKE) -C demo

clean:
	rm -f *.o $(LIB)
	$(MAKE) -C test clean
	$(MAKE) -C demo clean

.PHONY: all lint test demo clean
