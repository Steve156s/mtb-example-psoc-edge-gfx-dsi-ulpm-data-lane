/*******************************************************************************
* File Name        : imu_bmi270.h
*
* Description      : BMI270 6-axis IMU service for the CM55 application.
*                    - Reads accelerometer (+/-16 g) and gyroscope
*                      (+/-2000 dps) at 50 Hz over the shared
*                      CYBSP_I2C_CONTROLLER bus (Bosch BMI270_SensorAPI via
*                      Infineon sensor-motion-bmi270).
*                    - Computes pitch / roll and detects free fall, impact
*                      and fall (see imu_motion.h).
*                    - Optionally draws a small read-out on the frame buffer.
*
*                    The getters below are thread-safe and intended to be
*                    shared with other sensor services, e.g. BMM350 tilt
*                    compensated heading uses imu_get_attitude().
*
*******************************************************************************/
#ifndef IMU_BMI270_H
#define IMU_BMI270_H

#include <stdbool.h>
#include <stdint.h>

#include "cy_result.h"
#include "cy_scb_i2c.h"

#include "imu_motion.h"
#include "hud_text.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*******************************************************************************
* Configuration (override with DEFINES+= in proj_cm55/Makefile)
*******************************************************************************/
#ifndef IMU_SAMPLE_PERIOD_MS
#define IMU_SAMPLE_PERIOD_MS        (20U)   /* 50 Hz processing              */
#endif

#ifndef IMU_HUD_PERIOD_MS
#define IMU_HUD_PERIOD_MS           (100U)  /* 10 Hz screen refresh          */
#endif

#ifndef IMU_LOG_PERIOD_MS
#define IMU_LOG_PERIOD_MS           (500U)  /* UART log, 0 = off             */
#endif

#ifndef IMU_EVENT_HOLD_MS
#define IMU_EVENT_HOLD_MS           (3000U) /* how long IMPACT/FALL is shown */
#endif

/* Mounting: map raw sensor axes to the goggle frame (X forward, Y left,
 * Z up when level). Default is identity = sensor frame of the kit. */
#ifndef IMU_AXIS_REMAP
#define IMU_AXIS_REMAP(in, out)     do { (out)[0] = (in)[0]; \
                                         (out)[1] = (in)[1]; \
                                         (out)[2] = (in)[2]; } while (0)
#endif

/*******************************************************************************
* Types
*******************************************************************************/
typedef struct
{
    bool     valid;            /* false until the sensor delivered data     */
    uint32_t timestamp_ms;     /* RTOS tick time of the sample              */
    float    accel_g[3];       /* after remap                               */
    float    gyro_dps[3];      /* after remap, bias removed                 */
    float    accel_norm_g;
    float    pitch_deg;        /* +nose up   (about +Y)                     */
    float    roll_deg;         /* +right down (about +X)                    */
    uint32_t events_latched;   /* IMU_EVENT_* since last imu_take_events()  */
    uint32_t last_event;       /* most recent IMU_EVENT_* bit               */
    uint32_t last_event_ms;
    float    last_impact_g;
} imu_state_t;

/*******************************************************************************
* API
*******************************************************************************/

/*
 * Starts the IMU service task.
 *   i2c_ctx : context of the already initialised and enabled
 *             CYBSP_I2C_CONTROLLER (shared with the display).
 *   hud     : frame buffer to draw the read-out on, or NULL for none.
 *             The surface is copied; the buffer must stay valid.
 * Returns CY_RSLT_SUCCESS if the task was created. Sensor init happens in
 * the task; check imu_get_state().valid or the UART log.
 */
cy_rslt_t imu_bmi270_start(cy_stc_scb_i2c_context_t *i2c_ctx,
                           const hud_surface_t *hud);

/* Copy of the latest state. Returns state.valid. */
bool imu_get_state(imu_state_t *out);

/* Latest pitch / roll in degrees. Returns false if not yet valid.
 * Either pointer may be NULL. */
bool imu_get_attitude(float *pitch_deg, float *roll_deg);

/* Returns and clears the latched IMU_EVENT_* bits. */
uint32_t imu_take_events(void);

#if defined(__cplusplus)
}
#endif

#endif /* IMU_BMI270_H */
