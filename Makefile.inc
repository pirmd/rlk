RLK_DIR ?= .

RLK_CFLAGS = -I$(RLK_DIR)
RLK_LDLIBS = -L$(RLK_DIR) -lrlk

RLK_HDR = $(RLK_DIR)/err.h $(RLK_DIR)/tilemap.h
