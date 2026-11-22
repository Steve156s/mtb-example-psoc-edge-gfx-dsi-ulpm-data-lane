# Host build of the radar DSP / gesture / HUD modules (no ModusToolbox).
#   make run        build and run the tests
SRC_DIR := ../../proj_cm55/radar
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
CFLAGS  += -I$(SRC_DIR)

test_radar: test_radar.c $(SRC_DIR)/radar_dsp.c $(SRC_DIR)/radar_gesture.c $(SRC_DIR)/radar_hud.c $(wildcard $(SRC_DIR)/*.h)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) -lm

run: test_radar
	./test_radar

clean:
	rm -f test_radar radar_hud_preview.ppm

.PHONY: run clean
