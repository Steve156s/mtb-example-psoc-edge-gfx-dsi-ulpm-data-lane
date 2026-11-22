/*******************************************************************************
* File Name        : audio_i2s_psoc.h
*
* Description      : I2S output on TDM0 TX (BSP CYBSP_TDM_CONTROLLER_0:
*                    master, 16 bit, 2 channels, FIFO trigger 64 words).
*                    Mono ping-pong blocks are filled by the audio task; the
*                    TX FIFO interrupt copies them to both channels and
*                    notifies the task when a block has been consumed.
*
*******************************************************************************/
#ifndef AUDIO_I2S_PSOC_H
#define AUDIO_I2S_PSOC_H

#include <stdbool.h>
#include <stdint.h>

#include "cy_result.h"
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-time init: TDM block and its interrupt. notify = task to wake. */
cy_rslt_t audio_i2s_init(TaskHandle_t notify);

/* Starts the stream (outputs silence until blocks are committed) */
void audio_i2s_start(void);

/* Stops the stream and flushes the FIFO */
void audio_i2s_stop(void);

bool audio_i2s_running(void);

/* Returns a block the task may fill (AUDIO_BLOCK_SAMPLES mono samples), or
 * NULL if both are queued. Commit it when it is full. */
int16_t* audio_i2s_get_free_block(void);
void     audio_i2s_commit_block(int16_t* block);

/* true when both blocks have been played out (nothing queued) */
bool     audio_i2s_idle(void);

uint32_t audio_i2s_underruns(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_I2S_PSOC_H */
