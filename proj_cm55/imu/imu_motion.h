/*******************************************************************************
* File Name        : imu_motion.h
*
* Description      : Hardware-independent motion processing for a 6-axis IMU:
*                    - complementary filter for pitch / roll
*                    - free-fall, impact and fall detection
*
*                    This module has no MCU / RTOS dependencies so that it can
*                    be unit-tested on a host PC (see tools/imu_host_test).
*
* Coordinate frame : Right-handed sensor frame after axis remapping.
*                    At rest and level, accel = (0, 0, +1 g).
*                    roll  = rotation about +X, pitch = rotation about +Y.
*
*******************************************************************************/
#ifndef IMU_MOTION_H
#define IMU_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*******************************************************************************
* Event bits (returned by imu_motion_update and latched by imu_bmi270)
*******************************************************************************/
#define IMU_EVENT_NONE        (0x00UL)
#define IMU_EVENT_FREEFALL    (0x01UL)  /* |a| stayed below free-fall level   */
#define IMU_EVENT_IMPACT      (0x02UL)  /* |a| peak above impact level        */
#define IMU_EVENT_FALL        (0x04UL)  /* impact, then still and tipped over */

/*******************************************************************************
* Tunable parameters. Defaults are first guesses and MUST be tuned with
* recorded ski / MTB data before product use.
*******************************************************************************/
typedef struct
{
    float    filter_alpha;          /* gyro weight in complementary filter    */
    float    accel_trust_min_g;     /* use accel for tilt only inside this    */
    float    accel_trust_max_g;     /*   |a| window (else gyro only)          */

    float    freefall_g;            /* |a| below this = free fall             */
    uint32_t freefall_min_ms;       /* ...for at least this long              */

    float    impact_g;              /* |a| above this = impact                */

    uint32_t fall_window_ms;        /* after impact, look for "lying still"   */
    uint32_t still_min_ms;          /* still for this long => fall            */
    float    still_gyro_dps;        /* |gyro| below this counts as still      */
    float    still_accel_dev_g;     /* ||a| - 1 g| below this counts as still */
    float    fall_tilt_deg;         /* attitude change vs. pre-impact         */
} imu_motion_cfg_t;

/*******************************************************************************
* Filter / detector state (treat as opaque)
*******************************************************************************/
typedef struct
{
    imu_motion_cfg_t cfg;

    bool     attitude_valid;
    float    pitch_deg;
    float    roll_deg;
    float    accel_norm_g;
    float    gyro_norm_dps;

    /* Pre-event attitude (slowly tracked, frozen at impact) */
    float    ref_pitch_deg;
    float    ref_roll_deg;

    /* Free fall */
    uint32_t freefall_start_ms;
    bool     in_freefall;
    bool     freefall_reported;
    uint32_t last_freefall_ms;

    /* Impact / fall */
    bool     impact_active;         /* currently above impact threshold      */
    bool     fall_watch;            /* waiting for "still" after an impact   */
    bool     impact_after_freefall; /* informational: free fall < 1 s before */
    uint32_t impact_ms;
    float    impact_peak_g;
    float    last_impact_peak_g;
    uint32_t still_start_ms;
    bool     is_still;
} imu_motion_t;

/* Fill cfg with default thresholds. */
void imu_motion_default_cfg(imu_motion_cfg_t *cfg);

/* Reset state. cfg may be NULL to use defaults. */
void imu_motion_init(imu_motion_t *m, const imu_motion_cfg_t *cfg);

/*
 * Feed one sample.
 *   accel_g  : acceleration in g (sensor frame, after remap)
 *   gyro_dps : angular rate in deg/s (bias removed)
 *   dt_s     : time since previous sample
 *   now_ms   : monotonic time stamp
 * Returns the IMU_EVENT_* bits that became true on this sample.
 */
uint32_t imu_motion_update(imu_motion_t *m, const float accel_g[3],
                           const float gyro_dps[3], float dt_s,
                           uint32_t now_ms);

/* Peak |a| of the most recent impact, in g. */
float imu_motion_last_impact_g(const imu_motion_t *m);

#if defined(__cplusplus)
}
#endif

#endif /* IMU_MOTION_H */
