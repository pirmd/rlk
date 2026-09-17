# rlk - Minimal Grid Library for Roguelikes / 2D-Grid Games
LIB = librlk.a
SRC = grid.c view.c

-include config.mk

CC     ?= cc
CFLAGS ?= -O0 -g -std=c99 -Wall -Wextra

include Makefile.inc

CFLAGS += $(RLK_CFLAGS)

CFLAGS_LINT  = -Wall -Wextra
CFLAGS_LINT += -Wpedantic -Wshadow -Wvla
CFLAGS_LINT += -Wwrite-strings -Wold-style-definition
CFLAGS_LINT += -Wno-unused-function
CFLAGS_LINT += -Wconversion

all: $(LIB)

OBJ = $(SRC:.c=.o)

$(LIB): $(OBJ)
	ar rcs $@ $?

$(OBJ): $(RLK_HDR)

lint:
	$(CC) -Werror -fsyntax-only $(CFLAGS) $(CFLAGS_LINT) $(SRC) $(RLK_HDR)

test: $(LIB)
	$(MAKE) -C test test

demo: $(LIB)
	$(MAKE) -C demo all

clean:
	rm -f *.o $(LIB)
	$(MAKE) -C test clean
	$(MAKE) -C demo clean

.PHONY: all lint test demo clean
