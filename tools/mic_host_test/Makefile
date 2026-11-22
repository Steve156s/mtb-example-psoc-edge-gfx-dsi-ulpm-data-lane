# Host-side unit test for the microphone level meter / VAD (no ModusToolbox).
#   make        build and run
#   make clean
SRC_DIR := ../../proj_cm55/mic
LIB_CFLAGS := -std=c99 -O2 -Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -I$(SRC_DIR)

test_mic: test_mic.c mic_level.o mic_hud.o
	$(CC) -std=c99 -O2 -Wall -I$(SRC_DIR) -o $@ $^ -lm

%.o: $(SRC_DIR)/%.c
	$(CC) $(LIB_CFLAGS) -c -o $@ $<

.PHONY: run clean
run: test_mic
	./test_mic

.DEFAULT_GOAL := run

clean:
	rm -f test_mic *.o mic_hud_preview.ppm
