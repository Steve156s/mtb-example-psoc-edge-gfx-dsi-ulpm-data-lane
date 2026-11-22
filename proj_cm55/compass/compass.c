/*******************************************************************************
* File Name        : compass.c
*
* Description      : BMM350 tilt compensated e-compass service. Uses the
*                    Bosch Sensortec BMM350 SensorAPI (BSD-3-Clause) found
*                    under compass/bosch/bmm350/. Gravity for tilt
*                    compensation comes from the BMI270 IMU service
*                    (imu/imu_bmi270.h), so the BMI270 is driven by one
*                    module only.
*
*                    Flow (every COMPASS_SAMPLE_PERIOD_MS):
*                      BMM350 compensated XYZ (uT) -> axis map -> calibration
*                      imu_get_state().accel_g     (already in body frame)
*                      -> tilt compensated heading -> low-pass -> declination
*
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "compass.h"
#include "compass_config.h"
#include "compass_math.h"
#include "mag_i2c.h"

#include "bmm350.h"
#if (COMPASS_USE_BMI270_TILT_COMP)
#include "imu_bmi270.h"
#endif

/*******************************************************************************
* Macros
*******************************************************************************/
#define COMPASS_TASK_NAME               ("Compass Task")
#define COMPASS_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 6)

/* Accelerometer sample older than this is not used for tilt compensation */
#define COMPASS_ACC_MAX_AGE_MS          (200U)

/*******************************************************************************
* Static data
*******************************************************************************/
static const int8_t mag_axis_map[3][3] = COMPASS_MAG_AXIS_MAP;
static const compass_vec3_t forward_axis = COMPASS_FORWARD_AXIS;

static mag_i2c_dev_t bmm350_intf;
static struct bmm350_dev bmm350;

static compass_cal_t cal;
static compass_cal_collector_t cal_collector;
static compass_heading_filter_t heading_filter;

static volatile bool cal_toggle_request = false;

static compass_state_t shared_state;


/*******************************************************************************
* Bosch API glue
*******************************************************************************/
static int8_t bosch_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    const mag_i2c_dev_t *dev = (const mag_i2c_dev_t *) intf_ptr;
    return mag_i2c_read_regs(dev->address, reg_addr, reg_data, len);
}

static int8_t bosch_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    const mag_i2c_dev_t *dev = (const mag_i2c_dev_t *) intf_ptr;
    return mag_i2c_write_regs(dev->address, reg_addr, reg_data, len);
}

static void bosch_delay_us(uint32_t period, void *intf_ptr)
{
    (void) intf_ptr;
    mag_i2c_delay_us(period);
}


/*******************************************************************************
* Helpers
*******************************************************************************/
static compass_vec3_t apply_axis_map(const int8_t map[3][3], float sx, float sy, float sz)
{
    compass_vec3_t r;
    r.x = ((float) map[0][0] * sx) + ((float) map[0][1] * sy) + ((float) map[0][2] * sz);
    r.y = ((float) map[1][0] * sx) + ((float) map[1][1] * sy) + ((float) map[1][2] * sz);
    r.z = ((float) map[2][0] * sx) + ((float) map[2][1] * sy) + ((float) map[2][2] * sz);
    return r;
}

static void publish(const compass_state_t *s)
{
    taskENTER_CRITICAL();
    shared_state = *s;
    taskEXIT_CRITICAL();
}


/*******************************************************************************
* Function Name: bmm350_setup
********************************************************************************
* Summary:
*  Probes 0x14 / 0x15, initializes BMM350 (OTP load, magnetic reset) and puts
*  it in normal mode at 25 Hz with 4x averaging (low noise mode).
*******************************************************************************/
static int8_t bmm350_setup(void)
{
    static const uint8_t addrs[2] = { COMPASS_BMM350_I2C_ADDR_PRIMARY, COMPASS_BMM350_I2C_ADDR_SECONDARY };
    int8_t rslt = BMM350_E_NULL_PTR;

    for (uint32_t i = 0U; i < 2U; i++)
    {
        if (!mag_i2c_probe(addrs[i]))
        {
            continue;
        }

        memset(&bmm350, 0, sizeof(bmm350));
        bmm350_intf.address = addrs[i];
        bmm350.intf_ptr = &bmm350_intf;
        bmm350.read     = bosch_i2c_read;
        bmm350.write    = bosch_i2c_write;
        bmm350.delay_us = bosch_delay_us;

        rslt = bmm350_init(&bmm350);
        if (BMM350_OK == rslt)
        {
            printf("[compass] BMM350 found at 0x%02X (chip id 0x%02X)\r\n",
                   (unsigned int) addrs[i], (unsigned int) bmm350.chip_id);
            break;
        }
    }

    if (BMM350_OK != rslt)
    {
        return rslt;
    }

    rslt = bmm350_set_odr_performance(BMM350_DATA_RATE_25HZ, BMM350_AVERAGING_4, &bmm350);

    if (BMM350_OK == rslt)
    {
        rslt = bmm350_enable_axes(BMM350_X_EN, BMM350_Y_EN, BMM350_Z_EN, &bmm350);
    }

    if (BMM350_OK == rslt)
    {
        rslt = bmm350_set_powermode(BMM350_NORMAL_MODE, &bmm350);
    }

    return rslt;
}




/*******************************************************************************
* Function Name: get_gravity
********************************************************************************
* Summary:
*  Fetches the latest "up" vector (accelerometer, g, body frame) from the
*  BMI270 IMU service. Returns false if no fresh sample is available.
*******************************************************************************/
static bool get_gravity(compass_vec3_t *up)
{
#if (COMPASS_USE_BMI270_TILT_COMP)
    imu_state_t imu;
    uint32_t now = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);

    if (imu_get_state(&imu) && imu.valid &&
        ((now - imu.timestamp_ms) <= COMPASS_ACC_MAX_AGE_MS))
    {
        up->x = imu.accel_g[0];
        up->y = imu.accel_g[1];
        up->z = imu.accel_g[2];
        return true;
    }
#else
    (void) up;
#endif
    return false;
}


/*******************************************************************************
* Function Name: compass_task
*******************************************************************************/
static void compass_task(void *arg)
{
    compass_state_t st;
    struct bmm350_mag_temp_data mag_data;
    compass_vec3_t mag_body, mag_cal, acc_body;
    bool acc_ok;
    TickType_t last_wake;
    TickType_t cal_start = 0;
    uint32_t consecutive_errors = 0U;
    float heading;
    int8_t rslt;

    (void) arg;

    memset(&st, 0, sizeof(st));
    st.status = COMPASS_STATUS_INIT;
    publish(&st);

    compass_cal_reset(&cal);
    memset(&heading_filter, 0, sizeof(heading_filter));

    rslt = bmm350_setup();
    if (BMM350_OK != rslt)
    {
        printf("[compass] BMM350 not available (rslt %d)\r\n", (int) rslt);
        st.status = COMPASS_STATUS_NO_SENSOR;
        publish(&st);
        vTaskDelete(NULL);
        return;
    }

    printf("[compass] running. Press USER BTN2 and rotate the board in a figure-8 to calibrate\r\n");

    last_wake = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(COMPASS_SAMPLE_PERIOD_MS));

        /* ---- calibration state machine ---------------------------------- */
        if (cal_toggle_request)
        {
            cal_toggle_request = false;

            if (COMPASS_STATUS_CALIBRATING != st.status)
            {
                compass_cal_collector_start(&cal_collector);
                cal_start = xTaskGetTickCount();
                st.status = COMPASS_STATUS_CALIBRATING;
                st.cal_progress = 0U;
                printf("[compass] calibration started\r\n");
            }
            else
            {
                /* finish early: force the timeout path below */
                cal_start = xTaskGetTickCount() - pdMS_TO_TICKS(COMPASS_CAL_DURATION_MS) - 1U;
            }
        }

        /* ---- sample ----------------------------------------------------- */
        rslt = bmm350_get_compensated_mag_xyz_temp_data(&mag_data, &bmm350);
        if (BMM350_OK != rslt)
        {
            if (++consecutive_errors > 10U)
            {
                st.status = COMPASS_STATUS_ERROR;
                publish(&st);
            }
            continue;
        }
        consecutive_errors = 0U;

        mag_body = apply_axis_map(mag_axis_map, mag_data.x, mag_data.y, mag_data.z);

        /* Without a fresh BMI270 sample the board is assumed level */
        acc_ok = get_gravity(&acc_body);
        st.tilt_compensated = acc_ok;

        if (COMPASS_STATUS_CALIBRATING == st.status)
        {
            compass_cal_collector_add(&cal_collector, &mag_body);
            st.cal_progress = compass_cal_collector_progress(&cal_collector);

            if ((xTaskGetTickCount() - cal_start) > pdMS_TO_TICKS(COMPASS_CAL_DURATION_MS))
            {
                compass_cal_t new_cal;

                if (compass_cal_collector_finish(&cal_collector, &new_cal))
                {
                    cal = new_cal;
                    /* integers only: newlib-nano printf has no %f by default */
                    printf("[compass] calibration OK: offset(%d, %d, %d) uT scale x1000(%d, %d, %d)\r\n",
                           (int) cal.offset.x, (int) cal.offset.y, (int) cal.offset.z,
                           (int) (cal.scale.x * 1000.0f), (int) (cal.scale.y * 1000.0f),
                           (int) (cal.scale.z * 1000.0f));
                }
                else
                {
                    printf("[compass] calibration rejected: not enough rotation (%u%%), keeping previous\r\n",
                           (unsigned int) st.cal_progress);
                }

                st.status = COMPASS_STATUS_OK;
                memset(&heading_filter, 0, sizeof(heading_filter));
            }
        }
        else
        {
            st.status = COMPASS_STATUS_OK;
        }

        /* ---- heading ---------------------------------------------------- */
        mag_cal = compass_cal_apply(&cal, &mag_body);

        heading = compass_tilt_compensated_heading(acc_ok ? &acc_body : NULL,
                                                   &mag_cal, &forward_axis);
        if (heading >= 0.0f)
        {
            heading = compass_heading_filter_update(&heading_filter, heading,
                                                    COMPASS_HEADING_FILTER_ALPHA);
            st.heading_deg = compass_wrap_360(heading + COMPASS_DECLINATION_DEG);
        }

        st.field_ut      = compass_vec3_norm(&mag_cal);
        st.temperature_c = mag_data.temperature;
        st.cal_valid     = cal.valid;
        st.interference  = (st.field_ut < COMPASS_FIELD_MIN_UT) || (st.field_ut > COMPASS_FIELD_MAX_UT);
        st.sample_count++;

        publish(&st);
    }
}


/*******************************************************************************
* Public API
*******************************************************************************/
cy_rslt_t compass_start(CySCB_Type *i2c_base, cy_stc_scb_i2c_context_t *i2c_ctx,
                        UBaseType_t priority)
{
    cy_rslt_t result = mag_i2c_init(i2c_base, i2c_ctx);

    if (CY_RSLT_SUCCESS == result)
    {
        if (pdPASS != xTaskCreate(compass_task, COMPASS_TASK_NAME, COMPASS_TASK_STACK_SIZE,
                                  NULL, priority, NULL))
        {
            result = MAG_I2C_RSLT_ERR;
        }
    }

    return result;
}

void compass_request_calibration(void)
{
    cal_toggle_request = true;
}

void compass_get_state(compass_state_t *out)
{
    taskENTER_CRITICAL();
    *out = shared_state;
    taskEXIT_CRITICAL();
}
