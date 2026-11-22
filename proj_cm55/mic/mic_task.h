/*******************************************************************************
* File Name        : mic_task.h
*
* Description      : FreeRTOS task that runs the 4 microphones:
*                    - 2x PDM (D-L, D-R) and 2x analog (A-1, A-2), 16 kHz,
*                      10 ms frames
*                    - per-channel level / VU / peak hold / clip detection
*                    - sound activity detection (VAD) on the analog mics
*                    - "wake mode": PDM mics stay off until the analog VAD
*                      fires, then run for MIC_WAKE_HOLD_FRAMES
*                    - VU bars on the UART, and a view struct for the HUD
*
*                    Frame hook: mic_task_frame_hook() is called with every
*                    4-channel frame. It is a weak function so later work
*                    (noise reduction, wake word, PTT / Codec2 encoder) can
*                    plug in without touching this file.
*
*******************************************************************************/
#ifndef MIC_TASK_H
#define MIC_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "cy_result.h"
#include "mic_hud.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Start in wake mode (1) or with all four mics always on (0). */
#ifndef MIC_WAKE_MODE_DEFAULT
#define MIC_WAKE_MODE_DEFAULT   (0)
#endif

/* Creates the microphone task. */
cy_rslt_t mic_task_start(uint32_t priority);

/* Toggle always-on <-> wake mode. Safe to call from an ISR (button). */
void mic_task_request_mode_toggle(void);

/* Snapshot for the HUD (thread safe). */
void mic_task_get_view(mic_hud_view_t *view);

/* Optional consumer of the raw audio (16-bit, DC removed, 160 samples per
 * channel). ch[] order: D-L, D-R, A-1, A-2; valid[i] is false if that
 * channel produced no frame this time (e.g. PDM off in wake mode).
 * Runs in the mic task context: keep it short or hand off to another task. */
void mic_task_frame_hook(const int16_t *const ch[4], const bool valid[4]);

#if defined(__cplusplus)
}
#endif

#endif /* MIC_TASK_H */

/* [] END OF FILE */
