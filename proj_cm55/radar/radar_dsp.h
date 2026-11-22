/*******************************************************************************
* File Name        : radar_dsp.h
*
* Description      : Per-frame signal processing of the BGT60TR13C raw FIFO
*                    data. Platform independent (no PDL / RTOS), so it is also
*                    built by tools/radar_host_test.
*
*                    Frame layout (as read from the FIFO):
*                      raw[((chirp * SAMPLES) + sample) * RX + antenna]
*                    12-bit unsigned ADC values.
*
*                    Processing per frame:
*                      1. per chirp and antenna: remove DC, Hann window,
*                         64-point FFT -> 32 range bins (~3.75 cm each)
*                      2. subtract the static clutter map (EMA of the
*                         chirp-averaged spectrum) -> moving / new targets
*                      3. energy per range bin, strongest bin inside the
*                         detection window, adaptive noise floor
*                      4. at that bin: azimuth (RX1-RX3 phase), elevation
*                         (RX2-RX3 phase) and radial velocity (chirp-to-chirp
*                         phase)
*
*******************************************************************************/
#ifndef RADAR_DSP_H
#define RADAR_DSP_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define RADAR_DSP_SAMPLES       (64U)
#define RADAR_DSP_CHIRPS        (32U)
#define RADAR_DSP_RX            (3U)
#define RADAR_DSP_BINS          (RADAR_DSP_SAMPLES / 2U)
#define RADAR_DSP_FRAME_SAMPLES (RADAR_DSP_SAMPLES * RADAR_DSP_CHIRPS * RADAR_DSP_RX)

/* Physical constants of the gesture preset (see radar_settings.h) */
#define RADAR_DSP_BANDWIDTH_HZ  (4.0e9f)
#define RADAR_DSP_CENTER_HZ     (60.5e9f)
#define RADAR_DSP_CHIRP_TIME_S  (0.000299787f)
#define RADAR_DSP_LIGHT_SPEED   (2.99792458e8f)
/* Range covered by one FFT bin: c / (2 * B) */
#define RADAR_DSP_BIN_CM        ((RADAR_DSP_LIGHT_SPEED / (2.0f * RADAR_DSP_BANDWIDTH_HZ)) * 100.0f)

typedef struct
{
    bool     present;       /* a moving hand was detected in this frame     */
    float    range_cm;      /* distance of the strongest reflector          */
    float    azimuth_deg;   /* + = towards RX_AZ_A side (x RADAR_AZ_SIGN)   */
    float    elevation_deg; /* + = towards RX_EL_A side (x RADAR_EL_SIGN)   */
    float    velocity_mps;  /* radial; negative = approaching               */
    float    energy;        /* peak energy (after clutter removal)          */
    float    noise;         /* current noise floor estimate                 */
    uint32_t peak_bin;
} radar_frame_result_t;

/* How the static clutter map is updated in this frame */
typedef enum
{
    RADAR_CLUTTER_LEARN = 0,    /* idle: learn, but not while a target is seen */
    RADAR_CLUTTER_FREEZE,       /* a hand is being tracked: do not learn       */
    RADAR_CLUTTER_ABSORB        /* after a gesture: learn even with a target,
                                   so a hand / object that stays is absorbed  */
} radar_clutter_mode_t;

typedef struct
{
    float    clutter_re[RADAR_DSP_RX][RADAR_DSP_BINS];
    float    clutter_im[RADAR_DSP_RX][RADAR_DSP_BINS];
    float    noise;
    bool     initialized;
    uint32_t frames;
} radar_dsp_t;

/** Reset the clutter map and the noise floor. */
void radar_dsp_init(radar_dsp_t* dsp);

/**
 * Process one complete frame.
 * @param dsp       state (clutter map, noise floor)
 * @param raw       RADAR_DSP_FRAME_SAMPLES 12-bit samples, interleaved RX
 * @param mode      clutter map update mode, from radar_gesture_clutter_mode()
 * @param out       result
 */
void radar_dsp_process(radar_dsp_t* dsp, const uint16_t* raw,
                       radar_clutter_mode_t mode, radar_frame_result_t* out);

#if defined(__cplusplus)
}
#endif

#endif /* RADAR_DSP_H */
