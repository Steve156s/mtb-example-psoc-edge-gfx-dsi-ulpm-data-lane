/*******************************************************************************
* File Name        : compass.h
*
* Description      : BMM350 (+ optional BMI270) tilt compensated e-compass
*                    service running as a FreeRTOS task.
*
*******************************************************************************/
#ifndef COMPASS_H
#define COMPASS_H

#include <stdbool.h>
#include <stdint.h>

#include "cy_pdl.h"
#include "FreeRTOS.h"
#include "task.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum
{
    COMPASS_STATUS_INIT = 0,        /* task started, sensor not ready yet   */
    COMPASS_STATUS_OK,              /* heading valid                        */
    COMPASS_STATUS_CALIBRATING,     /* collecting calibration data          */
    COMPASS_STATUS_NO_SENSOR,       /* BMM350 not found on the bus          */
    COMPASS_STATUS_ERROR            /* bus / sensor error                   */
} compass_status_t;

typedef struct
{
    compass_status_t status;
    float heading_deg;              /* filtered, declination applied, 0..360 */
    float field_ut;                 /* calibrated |B|                        */
    float temperature_c;            /* BMM350 die temperature                */
    uint8_t cal_progress;           /* 0..100 while CALIBRATING              */
    bool cal_valid;                 /* a calibration is being applied        */
    bool tilt_compensated;          /* BMI270 found and in use               */
    bool interference;              /* |B| outside the expected window       */
    uint32_t sample_count;
} compass_state_t;

/* Creates the compass task. The I2C controller must already be initialized,
 * enabled and have its interrupt hooked up. */
cy_rslt_t compass_start(CySCB_Type *i2c_base, cy_stc_scb_i2c_context_t *i2c_ctx,
                        UBaseType_t priority);

/* Start calibration, or finish it early if one is running. ISR safe. */
void compass_request_calibration(void);

/* Thread-safe snapshot of the latest result. */
void compass_get_state(compass_state_t *out);

/*
 * Tilt compensation hook (weak, default returns false = "assume level").
 * Override it once an accelerometer driver is available, e.g. with the
 * BMI270 module:
 *
 *   bool compass_port_get_gravity(float up[3])
 *   {
 *       imu_state_t imu;
 *       if (!imu_get_state(&imu) || !imu.valid) { return false; }
 *       up[0] = imu.accel_g[0]; up[1] = imu.accel_g[1]; up[2] = imu.accel_g[2];
 *       return true;
 *   }
 *
 * up[] is the accelerometer reading (any unit) in the same right-handed body
 * frame as COMPASS_MAG_AXIS_MAP; at rest and level it is (0, 0, +g).
 * Called from the compass task at COMPASS_SAMPLE_PERIOD_MS; must not block.
 */
bool compass_port_get_gravity(float up[3]);

#if defined(__cplusplus)
}
#endif

#endif /* COMPASS_H */
