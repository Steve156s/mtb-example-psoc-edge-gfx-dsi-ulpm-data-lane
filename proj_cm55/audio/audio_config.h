/*******************************************************************************
* File Name        : audio_config.h
*
* Description      : Build-time settings of the audio output (TLV320DAC3100
*                    DAC + I2S/TDM + prompt player). Every value can be
*                    overridden from proj_cm55/Makefile with DEFINES+=NAME=value.
*
*                    Hardware (KIT_PSE84_AI and KIT_PSE84_EVAL, same pins):
*                    - TLV320DAC3100 I2C address 0x18 on CYBSP_I2C_CONTROLLER
*                      (SCB0, P8.0 SCL / P8.1 SDA, shared with display/sensors)
*                    - I2S (TDM0 TX, master): MCLK P21.3, BCLK P21.2,
*                      DIN P21.1, WS P12.3. BSP setting: 16-bit, 2 channels,
*                      MCLK = 2.048 MHz, BCLK = MCLK / 4 = 512 kHz -> 16 kHz
*                    - Speaker: Class-D output on J8 (AI kit: connector not
*                      fitted, max. 2.5 W); headphone outputs HPL/HPR
*
*******************************************************************************/
#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

/* Sample rate of the I2S stream and of the prompt clips. Must match the
 * BSP TDM clock: MCLK (CYBSP_TDM_CONTROLLER_0_CLK_DIV) / txclockDiv / 32. */
#ifndef AUDIO_SAMPLE_RATE_HZ
#define AUDIO_SAMPLE_RATE_HZ            (16000U)
#endif

/* MCLK driven by the E84 on P21.3 */
#ifndef AUDIO_MCLK_HZ
#define AUDIO_MCLK_HZ                   (2048000U)
#endif

/* 1: DAC clocks from the codec PLL. tlv320_clock_calc() finds, for 2.048 MHz
 *    and 16 kHz: P 1, R 1, J 40 -> 81.92 MHz, NDAC 2, MDAC 8, DOSR 320
 *    (DAC_MOD_CLK 5.12 MHz). Meets all TLV320DAC3100 clock rules and enables
 *    the PRB_P25 block with the hardware beep generator.
 * 0: DAC clocked straight from MCLK (NDAC 1, MDAC 1, DOSR 128, PRB_P1),
 *    the setting used by Infineon's audio-codec-tlv320dac3100 library.
 *    Lower power but below the datasheet DAC_MOD_CLK minimum: fallback if
 *    the PLL setting does not lock on the board. */
#ifndef AUDIO_DAC_USE_PLL
#define AUDIO_DAC_USE_PLL               (1)
#endif

/* Output routing: 1 = speaker (Class-D, J8), 2 = headphone, 3 = both */
#define AUDIO_OUTPUT_SPEAKER            (1U)
#define AUDIO_OUTPUT_HEADPHONE          (2U)
#ifndef AUDIO_OUTPUT
#define AUDIO_OUTPUT                    (AUDIO_OUTPUT_SPEAKER | AUDIO_OUTPUT_HEADPHONE)
#endif

/* Class-D speaker driver gain: 6, 12, 18 or 24 dB. Start low: the speaker
 * of the prototype is small and J8 is limited to 2.5 W. */
#ifndef AUDIO_SPK_GAIN_DB
#define AUDIO_SPK_GAIN_DB               (6)
#endif

/* Volume steps 0..AUDIO_VOLUME_MAX. 0 = mute, MAX = 0 dB DAC digital gain,
 * each step below is AUDIO_VOLUME_STEP_DB quieter. */
#ifndef AUDIO_VOLUME_MAX
#define AUDIO_VOLUME_MAX                (10U)
#endif
#ifndef AUDIO_VOLUME_STEP_DB
#define AUDIO_VOLUME_STEP_DB            (3)
#endif
#ifndef AUDIO_VOLUME_DEFAULT
#define AUDIO_VOLUME_DEFAULT            (7U)
#endif

/* Mono samples per ping-pong block (16 ms at 16 kHz). The I2S interrupt
 * consumes AUDIO_FIFO_HALF frames per trigger; must divide the block. */
#ifndef AUDIO_BLOCK_SAMPLES
#define AUDIO_BLOCK_SAMPLES             (256U)
#endif
#define AUDIO_FIFO_HALF                 (32U)   /* 64 FIFO words = 32 L/R frames */

/* Silence between words of a spoken sequence and after a tone prompt */
#ifndef AUDIO_WORD_GAP_MS
#define AUDIO_WORD_GAP_MS               (40U)
#endif

/* After this long without anything to play, the I2S stream is stopped and
 * the DAC is muted and its speaker driver powered down. */
#ifndef AUDIO_IDLE_POWERDOWN_MS
#define AUDIO_IDLE_POWERDOWN_MS         (1500U)
#endif

/* FreeRTOS */
#ifndef AUDIO_TASK_PRIORITY
#define AUDIO_TASK_PRIORITY             (configMAX_PRIORITIES - 2)
#endif
#ifndef AUDIO_TASK_STACK_WORDS
#define AUDIO_TASK_STACK_WORDS          (configMINIMAL_STACK_SIZE * 4)
#endif
#ifndef AUDIO_QUEUE_LENGTH
#define AUDIO_QUEUE_LENGTH              (8U)
#endif
#ifndef AUDIO_I2S_IRQ_PRIORITY
#define AUDIO_I2S_IRQ_PRIORITY          (4U)    /* >= 1: may call FreeRTOS FromISR API */
#endif
#ifndef AUDIO_I2C_TIMEOUT_MS
#define AUDIO_I2C_TIMEOUT_MS            (20U)
#endif
#ifndef AUDIO_I2C_LOCK_TIMEOUT_MS
#define AUDIO_I2C_LOCK_TIMEOUT_MS       (100U)
#endif

#endif /* AUDIO_CONFIG_H */
