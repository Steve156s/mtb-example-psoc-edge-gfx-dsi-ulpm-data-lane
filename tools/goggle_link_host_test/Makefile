# Host unit test of the HUD Link codec, mailbox and LINK panel (no ModusToolbox)
#   make -C tools/goggle_link_host_test run
#   make -C tools/goggle_link_host_test preview   -> link_hud_preview.ppm

ROOT    := ../..
CC      ?= gcc
CFLAGS  ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion
CFLAGS  += -DGL_SHM_HOST_TEST -I$(ROOT)/shared/goggle_link -I$(ROOT)/proj_cm55/link_hud

SRCS    := test_goggle_link.c \
           $(ROOT)/shared/goggle_link/goggle_link_proto.c \
           $(ROOT)/shared/goggle_link/goggle_link_shm.c \
           $(ROOT)/proj_cm55/link_hud/goggle_link_hud.c

test_goggle_link: $(SRCS) $(wildcard $(ROOT)/shared/goggle_link/*.h) $(ROOT)/proj_cm55/link_hud/goggle_link_hud.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

run: test_goggle_link
	./test_goggle_link

preview: test_goggle_link
	./test_goggle_link link_hud_preview.ppm

clean:
	rm -f test_goggle_link link_hud_preview.ppm

.PHONY: run preview clean
