/*******************************************************************************
* File Name        : compass_math.h
*
* Description      : Hardware independent e-compass math:
*                      - hard-iron / soft-iron (diagonal) calibration
*                      - tilt compensated heading from accel + mag
*                      - heading low-pass filter that handles 359 -> 0 wrap
*
*                    Pure C (no PDL / RTOS dependency) so it can be unit
*                    tested on a host PC.
*
*******************************************************************************/
#ifndef COMPASS_MATH_H
#define COMPASS_MATH_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct
{
    float x;
    float y;
    float z;
} compass_vec3_t;

/* Calibration result applied as: m_cal = (m_raw - offset) * scale */
typedef struct
{
    compass_vec3_t offset;  /* hard-iron offset, uT */
    compass_vec3_t scale;   /* soft-iron diagonal scale, unitless */
    bool valid;
} compass_cal_t;

/* Running min/max collector used while the user rotates the device */
typedef struct
{
    compass_vec3_t min;
    compass_vec3_t max;
    uint32_t samples;
} compass_cal_collector_t;

/* Smoothed heading state (vector average of unit heading vectors) */
typedef struct
{
    float s;
    float c;
    bool primed;
} compass_heading_filter_t;

/* Minimum peak-to-peak range per axis (uT) accepted as a valid calibration.
 * Earth field is 25..65 uT, so a full rotation gives ~50..130 uT p-p. */
#define COMPASS_CAL_MIN_RANGE_UT        (30.0f)

void  compass_cal_reset(compass_cal_t *cal);
void  compass_cal_collector_start(compass_cal_collector_t *col);
void  compass_cal_collector_add(compass_cal_collector_t *col, const compass_vec3_t *m);
/* Returns true and fills *cal if the collected data covers enough rotation */
bool  compass_cal_collector_finish(const compass_cal_collector_t *col, compass_cal_t *cal);
/* 0..100 rough indication of how much of the sphere has been covered */
uint8_t compass_cal_collector_progress(const compass_cal_collector_t *col);

compass_vec3_t compass_cal_apply(const compass_cal_t *cal, const compass_vec3_t *m_raw);

/*
 * Tilt compensated heading.
 *   acc     : accelerometer reading in the same right-handed body frame as
 *             mag (any unit). At rest it points "up" (reaction to gravity).
 *             Pass NULL if no accelerometer is available: the device is then
 *             assumed to be level with +Z up.
 *   mag     : calibrated magnetometer reading (any unit).
 *   forward : body axis that defines "where the user is looking".
 * Returns magnetic heading in degrees [0, 360), or a negative value if the
 * input is degenerate (free fall, magnetic field parallel to gravity, ...).
 */
float compass_tilt_compensated_heading(const compass_vec3_t *acc,
                                       const compass_vec3_t *mag,
                                       const compass_vec3_t *forward);

/* Wrap any angle (deg) into [0, 360) */
float compass_wrap_360(float deg);

/* alpha in (0,1]: 1 = no filtering. Returns filtered heading in [0, 360). */
float compass_heading_filter_update(compass_heading_filter_t *f, float heading_deg, float alpha);

/* 8-point cardinal name ("N", "NE", "E", ...) */
const char *compass_cardinal_8(float heading_deg);

float compass_vec3_norm(const compass_vec3_t *v);

#if defined(__cplusplus)
}
#endif

#endif /* COMPASS_MATH_H */
