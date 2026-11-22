# Host build of the hardware independent RTC code (no ModusToolbox needed)
#   make run      -> unit tests + hud_preview.ppm / hud_preview_unset.ppm
SRC_DIR := ../../proj_cm55/rtc
CC      ?= cc
CFLAGS  += -std=c11 -Wall -Wextra -Werror -O2 -I$(SRC_DIR)

test_rtc: test_rtc.c $(SRC_DIR)/rtc_time.c $(SRC_DIR)/rtc_hud.c
	$(CC) $(CFLAGS) -o $@ $^

run: test_rtc
	./test_rtc

clean:
	rm -f test_rtc *.ppm

.PHONY: run clean
