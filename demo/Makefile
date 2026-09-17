# rlk - Examples
DEMO = demo_grid

RLK_DIR=..

-include $(RLK_DIR)/config.mk

CC     ?= cc
CFLAGS ?= -O0 -g -std=c99 -Wall -Wextra

include $(RLK_DIR)/Makefile.inc

CFLAGS += $(RLK_CFLAGS)
CFLAGS += $(RLK_DEMO_CFLAGS)
LDFLAGS += $(RLK_LDLIBS)
LDFLAGS += $(RLK_DEMO_LDLIBS)

all: $(DEMO)

$(DEMO): %: %.c $(RLK_DIR)/librlk.a $(PXL_DIR)/libpxl.a
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	@rm -f $(DEMO)

.PHONY: all clean
