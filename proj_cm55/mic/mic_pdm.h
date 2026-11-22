/*******************************************************************************
* File Name        : mic_pdm.h
*
* Description      : Stereo capture from the two IM73D122V01 digital PDM
*                    microphones through the PSOC Edge PDM-PCM hardware
*                    (CIC + FIR0 + FIR1 decimation, no CPU load). 24-bit words
*                    are converted to 16-bit with software gain.
*                    Output: 10 ms frames (160 samples per channel, 16 kHz).
*
*                    Based on Infineon mtb-example-psoc-edge-pdm-pcm and
*                    mtb-example-psoc-edge-mains-powered-local-voice
*                    (pdm_mic_interface.c).
*
*******************************************************************************/
#ifndef MIC_PDM_H
#define MIC_PDM_H

#include <stdbool.h>
#include <stdint.h>
#include "cy_result.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Error result returned by the microphone drivers */
#define MIC_RSLT_ERROR      CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_BASE, 0x4D1U)

/* Called from the PDM interrupt when a new 10 ms frame is ready. */
typedef void (*mic_frame_cb_t)(void);

/* Initialises the PDM block and both channels (does not start capture). */
cy_rslt_t mic_pdm_init(mic_frame_cb_t frame_ready_cb);

/* Starts / stops the conversion of both channels (Activate / DeActivate).
 * Whether the PDM clock pin also stops (mic sleep current) still has to be
 * measured on the board. */
void mic_pdm_start(void);
void mic_pdm_stop(void);
bool mic_pdm_is_running(void);

/* Copies the newest complete frame. Returns false if no new frame arrived
 * since the last call. */
bool mic_pdm_read_frame(int16_t *left, int16_t *right);

/* Number of frames the consumer did not pick up in time / FIFO errors */
uint32_t mic_pdm_overruns(void);
uint32_t mic_pdm_fifo_errors(void);

#if defined(__cplusplus)
}
#endif

#endif /* MIC_PDM_H */

/* [] END OF FILE */
