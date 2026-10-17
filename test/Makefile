# rlk - Unit tests

TST  = test_tilemap

RLK_DIR=..

-include $(RLK_DIR)/config.mk

CC     ?= cc
CFLAGS ?= -O0 -g -std=c99 -Wall -Wextra

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
