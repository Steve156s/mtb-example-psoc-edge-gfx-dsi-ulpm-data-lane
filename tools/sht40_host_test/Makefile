# Host build of the hardware independent SHT40 code (no ModusToolbox needed)
#   make run
SRC_DIR := ../../proj_cm55/humidity
CC      ?= cc
CFLAGS  += -std=c11 -Wall -Wextra -Werror -O2 -I$(SRC_DIR)

test_sht40: test_sht40.c $(SRC_DIR)/sht40_math.c $(SRC_DIR)/sht40_hud.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

run: test_sht40
	./test_sht40

clean:
	rm -f test_sht40 hud_preview.ppm

.PHONY: run clean
