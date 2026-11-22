# Host-side unit test for the BMM350 e-compass (no ModusToolbox needed).
#   make        build and run
#   make clean
SRC_DIR := ../../proj_cm55/compass
LIB_CFLAGS := -std=c99 -O2 -Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -I$(SRC_DIR)

test_compass: test_compass.c compass_math.o compass_hud.o
	$(CC) -std=c99 -O2 -Wall -I$(SRC_DIR) -o $@ $^ -lm

%.o: $(SRC_DIR)/%.c
	$(CC) $(LIB_CFLAGS) -c -o $@ $<

.PHONY: run clean
run: test_compass
	./test_compass

.DEFAULT_GOAL := run

clean:
	rm -f test_compass *.o hud_preview.ppm
