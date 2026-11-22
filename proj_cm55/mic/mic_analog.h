/*******************************************************************************
* File Name        : mic_analog.h
*
* Description      : Stereo capture from the two IM73A135V01 analog
*                    microphones through the PSOC Edge Autonomous Analog
*                    (LPPASS): CTB0 OpAmp0/OpAmp1 preamps -> HS SAR ADC
*                    (16x accumulation) -> 2x FIFO, paced by a 16 kHz TCPWM
*                    trigger. The Autonomous Controller runs the sequence
*                    without CPU; the CPU only empties the FIFO every 10 ms.
*
*                    Extends the mono "high quality AMIC" configuration of
*                    Infineon mtb-example-psoc-edge-battery-powered-local-voice
*                    (hs_adc_amic_config.c / hs_adc_amic_interface.c) to both
*                    microphones.
*
*******************************************************************************/
#ifndef MIC_ANALOG_H
#define MIC_ANALOG_H

#include <stdbool.h>
#include <stdint.h>
#include "cy_result.h"
#include "mic_pdm.h"    /* mic_frame_cb_t, MIC_RSLT_ERROR */

#if defined(__cplusplus)
extern "C" {
#endif

/* Configures pins, clocks, the 16 kHz timer, trigger routing and the
 * Autonomous Analog, then starts capture. */
cy_rslt_t mic_analog_init(mic_frame_cb_t frame_ready_cb);

/* Stops the timer and the Autonomous Controller. */
void mic_analog_stop(void);

/* Copies the newest complete frame (160 samples each). Returns false if no
 * new frame arrived since the last call. */
bool mic_analog_read_frame(int16_t *mic1, int16_t *mic2);

uint32_t mic_analog_overruns(void);

/* Actual sample rate in Hz from the timer clock (for the log). */
uint32_t mic_analog_sample_rate(void);

#if defined(__cplusplus)
}
#endif

#endif /* MIC_ANALOG_H */

/* [] END OF FILE */
