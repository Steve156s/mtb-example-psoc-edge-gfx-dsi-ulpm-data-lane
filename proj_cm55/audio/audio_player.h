/*******************************************************************************
* File Name        : audio_player.h
*
* Description      : Prompt / voice player for the HUD goggle prototype:
*                    TLV320DAC3100 DAC + I2S on PSOC Edge E84 (CM55).
*                    Thread-safe API: requests are queued to the audio task.
*                    Text-to-speech uses pre-recorded clips for now
*                    (whole phrases, plus word clips joined to speak numbers).
*
*******************************************************************************/
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "cy_pdl.h"
#include "audio_seq.h"
#include "tlv320dac3100.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AUDIO_PRIO_NORMAL = 0,   /* queued after what is playing */
    AUDIO_PRIO_URGENT        /* cuts the current prompt (SOS, alarms) */
} audio_prio_t;

typedef struct
{
    bool     dac_ok;         /* TLV320DAC3100 answered and is configured */
    bool     playing;
    bool     awake;          /* I2S running, speaker driver powered */
    uint8_t  volume;         /* 0..AUDIO_VOLUME_MAX */
    uint32_t played;         /* sequences finished or cut */
    uint32_t dropped;        /* requests rejected: queue full */
    uint32_t underruns;      /* I2S blocks not ready in time */
    tlv320_clock_t clock;
} audio_player_status_t;

/* Creates the audio task. The I2C controller must already be initialized
 * and enabled (its ISR calls Cy_SCB_I2C_Interrupt with the same context). */
cy_rslt_t audio_player_init(CySCB_Type* i2c_base, cy_stc_scb_i2c_context_t* i2c_context);

bool audio_play(const audio_seq_t* seq, audio_prio_t prio);
bool audio_play_tone(audio_tone_t tone, audio_prio_t prio);
bool audio_play_clip(audio_clip_id_t clip, audio_prio_t prio);
bool audio_play_tone_and_clip(audio_tone_t tone, audio_clip_id_t clip, audio_prio_t prio);

/* Pre-recorded TTS */
bool audio_say_speed_kmh(uint32_t kmh);
bool audio_say_altitude_m(int32_t m);
bool audio_say_battery_pct(uint32_t pct);
bool audio_say_heading_deg(uint32_t deg);

/* Stops playback and drops everything queued */
void audio_stop(void);

void    audio_set_volume(uint8_t level);     /* 0 = mute .. AUDIO_VOLUME_MAX */
uint8_t audio_get_volume(void);

void audio_player_get_status(audio_player_status_t* st);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
