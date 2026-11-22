/*******************************************************************************
* File Name        : mic_level.h
*
* Description      : Portable (no PDL / RTOS) per-channel audio helpers:
*                    DC removal, RMS / peak level in dBFS, VU ballistics with
*                    peak hold, clip counter, and an energy based sound
*                    activity detector (VAD) with adaptive noise floor.
*                    Integer only, so it runs on CM33 or CM55 and can be
*                    unit-tested on a PC (tools/mic_host_test).
*
*                    All levels are in 0.1 dB steps ("db10"): -600 = -60.0 dBFS.
*
*******************************************************************************/
#ifndef MIC_LEVEL_H
#define MIC_LEVEL_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Lowest level reported (digital silence) */
#define MIC_LEVEL_FLOOR_DB10            (-1000)

typedef struct
{
    /* settings */
    int16_t  vu_min_db10;           /* bar empty below this level */
    int16_t  release_db10;          /* VU decay per frame */
    uint16_t peak_hold_frames;
    int16_t  clip_level;            /* |sample| >= clip_level counts as clip */

    /* DC blocker state */
    int32_t  dc_x1;
    int32_t  dc_y1_q15;

    /* outputs, updated every frame */
    int16_t  rms_db10;              /* RMS level of the last frame */
    int16_t  peak_db10;             /* sample peak of the last frame */
    int16_t  vu_db10;               /* VU bar (fast attack, slow release) */
    int16_t  hold_db10;             /* peak-hold marker */
    uint16_t hold_count;
    uint32_t clip_count;            /* total clipped samples since reset */
    uint32_t frames;                /* processed frames */
} mic_level_t;

typedef enum
{
    MIC_VAD_EVENT_NONE = 0,
    MIC_VAD_EVENT_START,            /* sound activity started */
    MIC_VAD_EVENT_END               /* activity ended (after hangover) */
} mic_vad_event_t;

typedef struct
{
    /* settings */
    int16_t  on_db10;               /* level above floor to start */
    int16_t  off_db10;              /* level above floor to stay active */
    int16_t  abs_min_db10;          /* never trigger below this level */
    uint16_t on_frames;             /* consecutive loud frames to start */
    uint16_t hangover_frames;       /* quiet frames before END */

    /* state */
    int32_t  floor_db10_q8;         /* tracked noise floor, Q8 */
    bool     active;
    bool     floor_valid;
    uint16_t on_count;
    uint16_t quiet_count;
} mic_vad_t;

/* Converts a mean-square value of 16-bit samples (0 .. 2^30) to dBFS*10.
 * 0 dBFS = full-scale square wave. Returns MIC_LEVEL_FLOOR_DB10 for 0. */
int16_t mic_level_ms_to_db10(uint64_t mean_square);

/* Initialise with display / clip settings. */
void mic_level_init(mic_level_t *lv, int16_t vu_min_db10, int16_t release_db10,
                    uint16_t peak_hold_frames, int16_t clip_level);

/* Process one frame in place: removes DC from 'samples' and updates levels. */
void mic_level_process(mic_level_t *lv, int16_t *samples, uint32_t count);

/* VU bar fill 0..scale (e.g. scale = bar width in pixels) */
uint32_t mic_level_vu_fill(const mic_level_t *lv, int16_t db10, uint32_t scale);

void mic_vad_init(mic_vad_t *vad, int16_t on_db10, int16_t off_db10,
                  int16_t abs_min_db10, uint16_t on_frames,
                  uint16_t hangover_frames);

/* Feed one frame level (normally mic_level_t.rms_db10). */
mic_vad_event_t mic_vad_process(mic_vad_t *vad, int16_t level_db10);

/* Current noise floor estimate in dB*10 */
int16_t mic_vad_floor_db10(const mic_vad_t *vad);

#if defined(__cplusplus)
}
#endif

#endif /* MIC_LEVEL_H */

/* [] END OF FILE */
