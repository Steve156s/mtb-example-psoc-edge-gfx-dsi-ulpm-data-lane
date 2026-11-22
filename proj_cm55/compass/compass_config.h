/*******************************************************************************
* File Name        : compass_config.h
*
* Description      : Board / mounting dependent settings for the BMM350
*                    e-compass. Everything here is a compile-time default and
*                    can be overridden from the Makefile (DEFINES+=...).
*
*                    NOTE: Axis mapping and I2C address below have NOT yet
*                    been verified on real hardware (KIT_PSE84_EVAL /
*                    KIT_PSE84_AI). See README "BMM350 e-compass" section for
*                    the bring-up checklist.
*
*******************************************************************************/
#ifndef COMPASS_CONFIG_H
#define COMPASS_CONFIG_H

/* ---------------- I2C addresses (7-bit) ----------------------------------- */
/* BMM350: 0x14 (ADSEL low) or 0x15 (ADSEL high). Both are probed. */
#ifndef COMPASS_BMM350_I2C_ADDR_PRIMARY
#define COMPASS_BMM350_I2C_ADDR_PRIMARY     (0x14U)
#endif
#ifndef COMPASS_BMM350_I2C_ADDR_SECONDARY
#define COMPASS_BMM350_I2C_ADDR_SECONDARY   (0x15U)
#endif

/* 1: take gravity from the BMI270 IMU service (imu/imu_bmi270.h) for tilt
 * compensation. 0: heading is valid only when the board is held level. */
#ifndef COMPASS_USE_BMI270_TILT_COMP
#define COMPASS_USE_BMI270_TILT_COMP        (1)
#endif

/* ---------------- Sensor -> body frame mapping ---------------------------- */
/* Row i gives body axis i as a combination of BMM350 sensor axes (x, y, z).
 * Entries must be -1, 0 or +1 and the result must be a right-handed frame.
 * The body frame MUST be the same one the IMU service outputs after its
 * IMU_AXIS_REMAP (right-handed, +Z up when level, accel = (0, 0, +1 g)).
 * BMM350 and BMI270 are separate packages and their axes are not
 * necessarily parallel on the PCB: check both datasheets / the layout.
 * Bring-up check: rotate clockwise seen from above -> heading must increase;
 * tilt the board 30 deg while pointing at a fixed object -> heading must
 * stay within a few degrees.
 */
#ifndef COMPASS_MAG_AXIS_MAP
#define COMPASS_MAG_AXIS_MAP                { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }
#endif
/* Body axis the heading refers to ("where the wearer looks").
 * Board flat: +X. For a goggle where the PCB stands vertical behind the lens,
 * this is typically -Z or +Z of the board (verify on the mechanical design). */
#ifndef COMPASS_FORWARD_AXIS
#define COMPASS_FORWARD_AXIS                { 1.0f, 0.0f, 0.0f }
#endif

/* ---------------- Processing ---------------------------------------------- */
/* Magnetic declination (deg, east positive) added to get true north.
 * 0 = show magnetic heading. Look up the value for the resort with the
 * NOAA/BGS World Magnetic Model calculator; later this should be computed
 * from the GNSS position (WMM) instead of a constant. */
#ifndef COMPASS_DECLINATION_DEG
#define COMPASS_DECLINATION_DEG             (0.0f)
#endif

/* Sensor polling period */
#ifndef COMPASS_SAMPLE_PERIOD_MS
#define COMPASS_SAMPLE_PERIOD_MS            (40U)     /* 25 Hz */
#endif

/* Heading low-pass factor per sample (1.0 = off) */
#ifndef COMPASS_HEADING_FILTER_ALPHA
#define COMPASS_HEADING_FILTER_ALPHA        (0.25f)
#endif

/* Calibration window started by USER BTN2 (press again to finish early) */
#ifndef COMPASS_CAL_DURATION_MS
#define COMPASS_CAL_DURATION_MS             (20000U)
#endif

/* Calibrated field magnitude outside this window => magnetic interference */
#ifndef COMPASS_FIELD_MIN_UT
#define COMPASS_FIELD_MIN_UT                (20.0f)
#endif
#ifndef COMPASS_FIELD_MAX_UT
#define COMPASS_FIELD_MAX_UT                (80.0f)
#endif

#endif /* COMPASS_CONFIG_H */
