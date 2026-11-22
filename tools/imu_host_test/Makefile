# Host-side unit test for the IMU motion logic (no ModusToolbox needed).
#   make        build and run
#   make clean
SRC_DIR := ../../proj_cm55/imu
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion
CFLAGS  += -I$(SRC_DIR)

test_imu_motion: test_imu_motion.c $(SRC_DIR)/imu_motion.c $(SRC_DIR)/hud_text.c
	$(CC) $(CFLAGS) -Wno-conversion -Wno-double-promotion -o $@ $^ -lm

.PHONY: run clean
run: test_imu_motion
	./test_imu_motion

.DEFAULT_GOAL := run

clean:
	rm -f test_imu_motion hud_preview.ppm
