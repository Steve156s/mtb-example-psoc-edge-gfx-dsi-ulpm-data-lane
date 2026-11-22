# Host unit test of the portable audio code (no ModusToolbox needed):
#   make run            run the tests
#   make wav            also render every prompt to prompts_demo.wav
SRC_DIR := ../../proj_cm55/audio
SRCS    := test_audio.c $(SRC_DIR)/tlv320dac3100.c $(SRC_DIR)/audio_synth.c \
           $(SRC_DIR)/audio_adpcm.c $(SRC_DIR)/audio_seq.c $(SRC_DIR)/audio_clips.c
CFLAGS  := -std=c99 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion \
           -fsanitize=address,undefined -I$(SRC_DIR)

test_audio: $(SRCS) $(wildcard $(SRC_DIR)/*.h)
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm

run: test_audio
	./test_audio

wav: test_audio
	./test_audio prompts_demo.wav

clean:
	rm -f test_audio prompts_demo.wav

.PHONY: run wav clean
