/*******************************************************************************
* File Name        : radar_config.h
*
* Description      : Tunable parameters of the 60 GHz radar gesture pipeline.
*                    Every value can be overridden from proj_cm55/Makefile,
*                    for example  DEFINES+=RADAR_AZ_SIGN=-1
*
*                    None of these values has been tuned on hardware yet.
*
*******************************************************************************/
#ifndef RADAR_CONFIG_H
#define RADAR_CONFIG_H

/*------------------------------------------------------------------------------
* Antenna mapping (index in the interleaved FIFO stream: 0 = RX1, 1 = RX2,
* 2 = RX3). On the BGT60TR13C RX1/RX3 form the horizontal pair and RX2/RX3 the
* vertical pair. The sign depends on how the board / goggle is mounted: if a
* swipe to the right is reported as LEFT, set RADAR_AZ_SIGN to -1 (same for
* RADAR_EL_SIGN with UP / DOWN).
*-----------------------------------------------------------------------------*/
#ifndef RADAR_RX_AZ_A
#define RADAR_RX_AZ_A               (0)     /* RX1 */
#endif
#ifndef RADAR_RX_AZ_B
#define RADAR_RX_AZ_B               (2)     /* RX3 */
#endif
#ifndef RADAR_RX_EL_A
#define RADAR_RX_EL_A               (1)     /* RX2 */
#endif
#ifndef RADAR_RX_EL_B
#define RADAR_RX_EL_B               (2)     /* RX3 */
#endif
#ifndef RADAR_AZ_SIGN
#define RADAR_AZ_SIGN               (1)
#endif
#ifndef RADAR_EL_SIGN
#define RADAR_EL_SIGN               (1)
#endif

/*------------------------------------------------------------------------------
* Detection window. A gloved hand in front of the goggle is expected between
* ~6 cm and ~45 cm. Closer bins contain TX->RX leakage and the goggle frame.
*-----------------------------------------------------------------------------*/
#ifndef RADAR_RANGE_MIN_CM
#define RADAR_RANGE_MIN_CM          (6.0f)
#endif
#ifndef RADAR_RANGE_MAX_CM
#define RADAR_RANGE_MAX_CM          (45.0f)
#endif

/* Detection: peak energy must exceed noise floor x RADAR_SNR_FACTOR and the
 * absolute minimum RADAR_MIN_ENERGY (raw ADC LSB^2 units after FFT). */
#ifndef RADAR_SNR_FACTOR
#define RADAR_SNR_FACTOR            (8.0f)
#endif
#ifndef RADAR_MIN_ENERGY
#define RADAR_MIN_ENERGY            (2.0e5f)
#endif

/* Static clutter map (goggle frame, face, helmet): EMA of the chirp-averaged
 * range spectrum, learned while no hand is tracked (~0.6 s time constant).
 * It is frozen while a hand is tracked: any part of the hand learned into the
 * map would remain as a "ghost" target after the hand leaves. A hand that
 * stays still after a gesture is absorbed during the cooldown. */
#ifndef RADAR_CLUTTER_ALPHA_IDLE
#define RADAR_CLUTTER_ALPHA_IDLE    (0.05f)
#endif
#ifndef RADAR_CLUTTER_ALPHA_ACTIVE
#define RADAR_CLUTTER_ALPHA_ACTIVE  (0.0f)
#endif
#ifndef RADAR_NOISE_ALPHA
#define RADAR_NOISE_ALPHA           (0.05f)
#endif

/*------------------------------------------------------------------------------
* Gesture state machine (frames of ~30 ms)
*-----------------------------------------------------------------------------*/
#ifndef RADAR_GESTURE_ON_FRAMES
#define RADAR_GESTURE_ON_FRAMES     (2U)    /* consecutive detections to start  */
#endif
#ifndef RADAR_GESTURE_OFF_FRAMES
#define RADAR_GESTURE_OFF_FRAMES    (3U)    /* consecutive misses to end        */
#endif
#ifndef RADAR_GESTURE_MIN_FRAMES
#define RADAR_GESTURE_MIN_FRAMES    (4U)    /* shorter tracks are ignored       */
#endif
#ifndef RADAR_GESTURE_MAX_FRAMES
#define RADAR_GESTURE_MAX_FRAMES    (50U)   /* ~1.5 s: force a decision         */
#endif
#ifndef RADAR_GESTURE_COOLDOWN_FRAMES
#define RADAR_GESTURE_COOLDOWN_FRAMES (12U) /* ~360 ms dead time after gesture  */
#endif
#ifndef RADAR_GESTURE_EDGE_FRAMES
#define RADAR_GESTURE_EDGE_FRAMES   (3U)    /* frames averaged at start / end   */
#endif

/* Swipe: angle change in degrees, and dominance over the other axis */
#ifndef RADAR_SWIPE_MIN_DEG
#define RADAR_SWIPE_MIN_DEG         (25.0f)
#endif
#ifndef RADAR_SWIPE_DOMINANCE
#define RADAR_SWIPE_DOMINANCE       (1.5f)
#endif

/* Push: hand moves at least this much closer while it stays roughly in front
 * of the sensor (mean |azimuth| and mean |elevation| of the track) */
#ifndef RADAR_PUSH_MIN_CM
#define RADAR_PUSH_MIN_CM           (7.0f)
#endif
#ifndef RADAR_PUSH_MAX_DEG
#define RADAR_PUSH_MAX_DEG          (20.0f)
#endif

/*------------------------------------------------------------------------------
* HUD
*-----------------------------------------------------------------------------*/
/* How long the "<< SWIPE LEFT" toast stays on the panel */
#ifndef RADAR_HUD_TOAST_MS
#define RADAR_HUD_TOAST_MS          (1200U)
#endif

/* 1: PUSH starts the 6 s DSI ULPM (screen off) demo, same as USER BTN1 */
#ifndef RADAR_PUSH_ENTERS_ULPM
#define RADAR_PUSH_ENTERS_ULPM      (1)
#endif

#endif /* RADAR_CONFIG_H */
