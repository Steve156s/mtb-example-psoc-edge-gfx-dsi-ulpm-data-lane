/*******************************************************************************
* File Name        : audio_synth.h
*
* Description      : Prompt tone synthesizer (sine with linear sweep and a
*                    click-free attack / release envelope) and the tone
*                    prompt table. Portable, integer only, host-testable.
*
*******************************************************************************/
#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tone prompts */
typedef enum
{
    AUDIO_TONE_BOOT = 0,     /* rising C-E-G-C chime: power on */
    AUDIO_TONE_CLICK,        /* short tick: button feedback */
    AUDIO_TONE_CONFIRM,      /* two rising notes: action done */
    AUDIO_TONE_WARNING,      /* alternating 880 / 660 Hz: attention */
    AUDIO_TONE_LOW_BATTERY,  /* two falling notes */
    AUDIO_TONE_PTT_START,    /* up-sweep: talk now */
    AUDIO_TONE_PTT_END,      /* down-sweep: end of talk */
    AUDIO_TONE_MESSAGE,      /* double ping: message received */
    AUDIO_TONE_SOS,          /* ... --- ... at 1.8 kHz */
    AUDIO_TONE_ERROR,        /* low buzz */
    AUDIO_TONE_COUNT
} audio_tone_t;

/* One note. f_start = 0 means silence. f_end != f_start gives a sweep. */
typedef struct
{
    uint16_t f_start_hz;
    uint16_t f_end_hz;
    uint16_t ms;
    uint16_t amp;            /* peak amplitude, 0..32767 */
} audio_note_t;

typedef struct
{
    const audio_note_t* notes;
    uint8_t count;
    const char* name;
} audio_tone_def_t;

extern const audio_tone_def_t audio_tones[AUDIO_TONE_COUNT];

/* Generator state */
typedef struct
{
    const audio_note_t* notes;
    uint8_t  count;
    uint8_t  index;          /* current note */
    uint32_t pos;            /* sample within the note */
    uint32_t len;            /* samples of the note */
    uint32_t phase;          /* 32-bit phase accumulator */
    uint32_t fs_hz;
} audio_synth_t;

void   audio_synth_start(audio_synth_t* s, const audio_note_t* notes, uint8_t count, uint32_t fs_hz);

/* Renders up to n samples, returns the number written (< n at the end) */
size_t audio_synth_render(audio_synth_t* s, int16_t* out, size_t n);

/* Total length of a tone prompt in samples */
uint32_t audio_synth_length(const audio_note_t* notes, uint8_t count, uint32_t fs_hz);

/* Q15 sine of a 32-bit phase (0 .. 2^32 = 0 .. 2 pi), table + interpolation */
int16_t audio_sin_q15(uint32_t phase);

/* Phase increment for a frequency */
uint32_t audio_phase_inc(uint32_t f_hz, uint32_t fs_hz);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SYNTH_H */
