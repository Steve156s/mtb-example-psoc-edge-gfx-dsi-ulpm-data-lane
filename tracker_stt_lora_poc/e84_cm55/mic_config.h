/*******************************************************************************
* File Name        : mic_config.h
*
* Description      : Minimal microphone settings for the standalone STT POC
*                    (subset of PR #5 proj_cm55/mic/mic_config.h, PDM only).
*******************************************************************************/
#ifndef MIC_CONFIG_H
#define MIC_CONFIG_H

#define MIC_SAMPLE_RATE_HZ      (16000U)
#define MIC_FRAME_SAMPLES       (160U)      /* 10 ms */
#define MIC_ENABLE_PDM          (1)

/* AN242253: 24-bit FIFO words, then software gain to 16-bit */
#ifndef MIC_PDM_SW_GAIN_DB
#define MIC_PDM_SW_GAIN_DB      (20)
#endif
#ifndef MIC_PDM_IRQ_PRIORITY
#define MIC_PDM_IRQ_PRIORITY    (3U)
#endif

#endif /* MIC_CONFIG_H */
