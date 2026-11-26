/*******************************************************************************
* File Name        : stt_link.h
*
* Description      : Tracker STT POC on the CM55 (route C first, route A later):
*
*   PTT held  -> 16 kHz mono PCM is collected (stt_link_push_pcm)
*   PTT up    -> AUDIO_BEGIN / AUDIO_CHUNK... / AUDIO_END to the PC on the
*                KitProg3 debug UART
*   PC        -> TEXT (offline STT result) back on the same UART
*   E84       -> TEXT to the Heltec on a second UART; Heltec sends it over
*                LoRa and answers TX_RESULT, which is logged to the PC
*   Route A   -> stt_link_send_cmd(id) / stt_link_send_text(...) from an
*                on-device command recogniser (DEEPCRAFT / Cyberon)
*
* Frame format: link_frame.h. Not verified on hardware.
*******************************************************************************/
#ifndef STT_LINK_H
#define STT_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cy_result.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef STT_LINK_SAMPLE_RATE
#define STT_LINK_SAMPLE_RATE     (16000U)
#endif
#ifndef STT_LINK_MAX_SECONDS
#define STT_LINK_MAX_SECONDS     (8U)      /* 8 s x 16 kHz x 2 B = 256 KB */
#endif

/* Creates the link task. Needs the PC UART (CYBSP_DEBUG_UART) already
 * initialised by init_retarget_io(), and initialises the Heltec UART. */
cy_rslt_t stt_link_start(uint32_t priority);

/* Audio input: call with every mono 16-bit frame (e.g. from the mic task hook).
 * Samples are kept only while PTT is held. Safe from one producer task. */
void stt_link_push_pcm(const int16_t *samples, size_t count);

/* Route A / tests: send text or a command id straight to the Heltec. */
void stt_link_send_text(const char *utf8);
void stt_link_send_cmd(uint16_t id);

/* True while PTT is held (for a HUD / LED). */
bool stt_link_ptt_active(void);

#if defined(__cplusplus)
}
#endif

#endif /* STT_LINK_H */
