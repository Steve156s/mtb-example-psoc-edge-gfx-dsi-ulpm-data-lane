/*******************************************************************************
* File Name        : mic_config.h
*
* Description      : Compile-time settings for the 4-microphone capture demo
*                    (2x digital PDM IM73D122V01 + 2x analog IM73A135V01).
*                    Every value can be overridden from the Makefile, e.g.
*                    DEFINES+=MIC_PDM_SW_GAIN_DB=12
*
*                    NOTE: Not yet verified on real hardware. Gains and
*                    thresholds below are starting points taken from the
*                    Infineon code examples (pdm-pcm, mains-powered-local-voice,
*                    battery-powered-local-voice) and must be tuned on the board.
*
* Board wiring (KIT_PSE84_AI schematic Rev03, same pins on KIT_PSE84_EVAL):
*   PDM  CLK  P8.5 (CYBSP_PDM_CLK), DATA P8.6 (CYBSP_PDM_DATA)
*        both mics share one data line, L/R selected by the SELECT pin.
*        PDM receiver 2 = left (U8), receiver 3 = right (U7), 27 mm apart.
*   AMIC1  CTB0 OA0: INP P14.0, INN P14.1, OUT P14.2, REF P14.6
*   AMIC2  CTB0 OA1: INP P14.4, INN P14.5, OUT P14.3, REF P14.7
*   The opamps run open loop inside the CTB; gain/filter network is external
*   on the PCB (same as the Infineon battery-powered-local-voice example).
*
*******************************************************************************/
#ifndef MIC_CONFIG_H
#define MIC_CONFIG_H

/* ---------------- General ------------------------------------------------- */
/* Sample rate of all four channels (Hz). PDM rate is fixed by the decimation
 * chain in mic_pdm.c, analog rate by the TCPWM timer in mic_analog.c. */
#define MIC_SAMPLE_RATE_HZ              (16000U)

/* Frame length: 10 ms = 160 samples per channel (what AFE / wake-word /
 * Codec2 front ends expect). */
#define MIC_FRAME_SAMPLES               (160U)

/* Number of channels: 0,1 = PDM left/right, 2,3 = analog mic 1/2 */
#define MIC_NUM_CHANNELS                (4U)
#define MIC_CH_PDM_L                    (0U)
#define MIC_CH_PDM_R                    (1U)
#define MIC_CH_AMIC_1                   (2U)
#define MIC_CH_AMIC_2                   (3U)

/* Set to 0 to build without the analog microphones (PDM only). */
#ifndef MIC_ENABLE_ANALOG
#define MIC_ENABLE_ANALOG               (1)
#endif

/* Set to 0 to build without the digital microphones (analog only). */
#ifndef MIC_ENABLE_PDM
#define MIC_ENABLE_PDM                  (1)
#endif

/* ---------------- PDM (digital mics) -------------------------------------- */
/* AN242253: with 16-bit output and >29 dB hardware gain the IM7x DC offset
 * clips the left channel. We therefore capture 24-bit words and convert to
 * 16-bit in software with Cy_PDM_PCM_ApplyPCM_Gain (range -40..+40 dB). */
#ifndef MIC_PDM_SW_GAIN_DB
#define MIC_PDM_SW_GAIN_DB              (20)
#endif

/* PDM interrupt priority (lower number = higher priority) */
#ifndef MIC_PDM_IRQ_PRIORITY
#define MIC_PDM_IRQ_PRIORITY            (3U)
#endif

/* ---------------- Analog mics (Autonomous Analog / LPPASS) ---------------- */
/* SAR accumulation per sample (official example: >=16 recommended) */
#ifndef MIC_AMIC_ACCUMULATION
#define MIC_AMIC_ACCUMULATION           CY_AUTANALOG_SAR_ACC_CNT16
#endif

/* Preamp power. Lower power = less current but more noise. */
#ifndef MIC_AMIC_PREAMP_POWER
#define MIC_AMIC_PREAMP_POWER           CY_AUTANALOG_CTB_OA_PWR_ULTRA_HIGH_RAIL
#endif

/* Extra digital gain applied to the analog channels (dB, -40..+40). */
#ifndef MIC_AMIC_SW_GAIN_DB
#define MIC_AMIC_SW_GAIN_DB             (0)
#endif

/* LPPASS FIFO interrupt priority */
#ifndef MIC_AMIC_IRQ_PRIORITY
#define MIC_AMIC_IRQ_PRIORITY           (3U)
#endif

/* Free hardware used for the 16 kHz sample clock of the analog mics.
 * Checked against templates/TARGET_KIT_PSE84_EVAL_EPC2/config/design.modus:
 *   TCPWM0 group 1 counter 0 (= counter 256) is unused,
 *   peri 0 group 1 8-bit divider 4 is unused,
 *   peri 0 group 2 8-bit divider 0 is unused (charge pump clock of LPPASS).
 * Same resources as the Infineon battery-powered-local-voice example. */
#define MIC_AMIC_TCPWM_BASE             TCPWM0
#define MIC_AMIC_TCPWM_CNT              (256U)
#define MIC_AMIC_TCPWM_PCLK             PCLK_TCPWM0_CLOCK_COUNTER_EN256
#define MIC_AMIC_TCPWM_DIV_NUM          (4U)
#define MIC_AMIC_TCPWM_TRIG_IN          PERI_0_TRIG_IN_MUX_5_TCPWM0_TR_OUT0256
#define MIC_AMIC_TRIG_OUT               PERI_0_TRIG_OUT_MUX_5_PASS_TR_LPPASS_IN3
#define MIC_AMIC_PUMP_PCLK              PCLK_PASS_CLOCK_PUMP_PERI
#define MIC_AMIC_PUMP_DIV_NUM           (0U)
/* Pump clock divider (official example: 20) */
#define MIC_AMIC_PUMP_DIV_VALUE         (20U)

/* ---------------- Level meter / VU ---------------------------------------- */
/* VU display range in dBFS (bar empty at MIN, full at 0 dBFS) */
#define MIC_VU_MIN_DBFS                 (-72)

/* VU release (decay) per 10 ms frame in 0.1 dB; attack is instantaneous */
#define MIC_VU_RELEASE_DB10_PER_FRAME   (15)

/* Peak hold time in frames (1.5 s) */
#define MIC_VU_PEAK_HOLD_FRAMES         (150U)

/* A sample at or above this absolute 16-bit value counts as clipped */
#define MIC_CLIP_LEVEL                  (32000)

/* ---------------- Sound activity detection (analog mics) ------------------ */
/* Speech/sound is detected when the frame level exceeds the tracked noise
 * floor by MIC_VAD_ON_DB10 (0.1 dB) for MIC_VAD_ON_FRAMES consecutive frames.
 * It ends after MIC_VAD_HANGOVER_FRAMES quiet frames. */
#ifndef MIC_VAD_ON_DB10
#define MIC_VAD_ON_DB10                 (120)   /* +12 dB above floor */
#endif
#ifndef MIC_VAD_OFF_DB10
#define MIC_VAD_OFF_DB10                (60)    /* +6 dB (hysteresis) */
#endif
#ifndef MIC_VAD_ON_FRAMES
#define MIC_VAD_ON_FRAMES               (3U)    /* 30 ms */
#endif
#ifndef MIC_VAD_HANGOVER_FRAMES
#define MIC_VAD_HANGOVER_FRAMES         (80U)   /* 800 ms */
#endif
/* Absolute minimum level for detection (ignore a dead-quiet room) */
#ifndef MIC_VAD_ABS_MIN_DB10
#define MIC_VAD_ABS_MIN_DB10            (-600)  /* -60 dBFS */
#endif

/* Wake mode: after a detection the PDM mics stay on this long (frames) */
#ifndef MIC_WAKE_HOLD_FRAMES
#define MIC_WAKE_HOLD_FRAMES            (500U)  /* 5 s */
#endif

/* ---------------- Reporting ----------------------------------------------- */
/* UART VU print period in frames (10 = every 100 ms). 0 disables. */
#ifndef MIC_UART_PERIOD_FRAMES
#define MIC_UART_PERIOD_FRAMES          (10U)
#endif

#endif /* MIC_CONFIG_H */

/* [] END OF FILE */
