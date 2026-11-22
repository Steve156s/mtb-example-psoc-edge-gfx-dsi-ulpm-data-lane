/*******************************************************************************
* File Name        : radar_gesture.h
*
* Description      : Rule-based hand gesture recognizer working on the
*                    per-frame results of radar_dsp. Platform independent.
*
*                    A gesture is a "track": the hand appears (moving
*                    reflector inside 6..45 cm), moves, and disappears.
*                    When the track ends it is classified from the change of
*                    azimuth, elevation and range between its first and last
*                    frames:
*                      SWIPE_LEFT / SWIPE_RIGHT : azimuth change dominates
*                      SWIPE_UP   / SWIPE_DOWN  : elevation change dominates
*                      PUSH                     : hand came >= 7 cm closer
*                    It recognizes the same gesture set as Infineon's
*                    xensiv-radar-gestures library (PSOC 6), which can replace
*                    this module (or a DEEPCRAFT model) later.
*
*******************************************************************************/
#ifndef RADAR_GESTURE_H
#define RADAR_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "radar_dsp.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum
{
    RADAR_GESTURE_NONE = 0,
    RADAR_GESTURE_SWIPE_LEFT,
    RADAR_GESTURE_SWIPE_RIGHT,
    RADAR_GESTURE_SWIPE_UP,
    RADAR_GESTURE_SWIPE_DOWN,
    RADAR_GESTURE_PUSH,
    RADAR_GESTURE_COUNT
} radar_gesture_t;

typedef enum
{
    RADAR_TRACK_IDLE = 0,
    RADAR_TRACK_ARMING,
    RADAR_TRACK_ACTIVE,
    RADAR_TRACK_COOLDOWN
} radar_track_state_t;

#define RADAR_GESTURE_HISTORY   (64U)

typedef struct
{
    radar_track_state_t state;
    uint32_t on_count;
    uint32_t off_count;
    uint32_t cooldown;
    uint32_t n;                                 /* frames in the track */
    float    az[RADAR_GESTURE_HISTORY];
    float    el[RADAR_GESTURE_HISTORY];
    float    rng[RADAR_GESTURE_HISTORY];
    /* last classification details, for logging / HUD */
    float    last_d_az;
    float    last_d_el;
    float    last_d_rng;
    uint32_t last_frames;
} radar_gesture_ctx_t;

void radar_gesture_init(radar_gesture_ctx_t* ctx);

/** Feed one frame. Returns a gesture when a track ends, else NONE. */
radar_gesture_t radar_gesture_update(radar_gesture_ctx_t* ctx,
                                     const radar_frame_result_t* frame);

/** true while a hand is being followed */
bool radar_gesture_is_tracking(const radar_gesture_ctx_t* ctx);

/** Clutter map update mode for the next radar_dsp_process() call */
radar_clutter_mode_t radar_gesture_clutter_mode(const radar_gesture_ctx_t* ctx);

const char* radar_gesture_name(radar_gesture_t g);

#if defined(__cplusplus)
}
#endif

#endif /* RADAR_GESTURE_H */
