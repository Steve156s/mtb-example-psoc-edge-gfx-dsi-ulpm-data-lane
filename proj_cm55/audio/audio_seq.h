/*******************************************************************************
* File Name        : audio_seq.h
*
* Description      : Playback sequences (tone prompts, voice clips and gaps)
*                    and the sample source that renders them, plus the
*                    "pre-recorded TTS" builders that speak numbers by
*                    joining word clips (speed, altitude, battery, heading).
*                    Portable, host-testable.
*
*******************************************************************************/
#ifndef AUDIO_SEQ_H
#define AUDIO_SEQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_adpcm.h"
#include "audio_clips.h"
#include "audio_synth.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SEQ_MAX           (16U)

typedef enum
{
    AUDIO_SEG_TONE = 0,      /* arg = audio_tone_t */
    AUDIO_SEG_CLIP,          /* arg = audio_clip_id_t */
    AUDIO_SEG_SILENCE        /* arg = milliseconds */
} audio_seg_type_t;

typedef struct
{
    uint8_t  type;           /* audio_seg_type_t */
    uint16_t arg;
} audio_seg_t;

typedef struct
{
    audio_seg_t seg[AUDIO_SEQ_MAX];
    uint8_t count;
} audio_seq_t;

/* Builders. Return false if the sequence is full (nothing is added then). */
void audio_seq_clear(audio_seq_t* q);
bool audio_seq_add(audio_seq_t* q, audio_seg_type_t type, uint16_t arg);
bool audio_seq_add_number(audio_seq_t* q, uint32_t n);          /* 0..9999 */
bool audio_seq_speed_kmh(audio_seq_t* q, uint32_t kmh);         /* 時速 N 公里 */
bool audio_seq_altitude_m(audio_seq_t* q, int32_t m);           /* 海拔 N 公尺 */
bool audio_seq_battery_pct(audio_seq_t* q, uint32_t pct);       /* 電量 百分之 N */
bool audio_seq_heading_deg(audio_seq_t* q, uint32_t deg);       /* 方位 東北 ... */

/* Mandarin number words (Taiwan usage) as clip ids, 0..9999.
 * Returns the number of ids written, 0 if out of range or max too small. */
size_t audio_number_words(uint32_t n, uint16_t* ids, size_t max);

/* 8-sector compass direction clip for a heading in degrees */
audio_clip_id_t audio_heading_clip(uint32_t deg);

/* Source: renders a sequence into mono samples */
typedef struct
{
    audio_seq_t   seq;
    uint8_t       index;
    bool          seg_active;
    uint32_t      silence_left;
    uint32_t      fs_hz;
    uint32_t      word_gap;    /* samples of silence inserted between clips */
    bool          gap_pending;
    audio_synth_t synth;
    audio_adpcm_t adpcm;
} audio_source_t;

void   audio_source_start(audio_source_t* s, const audio_seq_t* q, uint32_t fs_hz, uint32_t word_gap_ms);
size_t audio_source_read(audio_source_t* s, int16_t* out, size_t n);   /* < n when done */
bool   audio_source_done(const audio_source_t* s);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SEQ_H */
