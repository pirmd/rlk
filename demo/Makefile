# Demo: pxl-based visualization of rlk region generation
# Build with: make -C demo
# Run with: ./demo_region

CC     ?= cc
CFLAGS ?= -O2 -g -std=c99 -Wall -Wextra

# pxl flags (adjust paths as needed)
PXL_DIR ?= ../pxl
PXL_CFLAGS = -I$(PXL_DIR)/include
PXL_LDFLAGS = -L$(PXL_DIR)/lib -lpxl -lSDL2

# rlk flags
RLK_DIR = ..
RLK_CFLAGS = -I$(RLK_DIR)

# Demo target
TGT = demo_region
SRC = demo.c

all: $(TGT)

$(TGT): $(SRC)
	$(CC) $(CFLAGS) $(PXL_CFLAGS) $(RLK_CFLAGS) $^ -o $@ $(PXL_LDFLAGS) -lm

clean:
	@rm -f $(TGT)

.PHONY: all clean
