/*******************************************************************************
* File Name        : imu_bmi270.c
*
* Description      : BMI270 IMU service task. See imu_bmi270.h.
*
* Note             : Not yet verified on hardware. Axis directions, thresholds
*                    and the I2C address must be confirmed on the kit.
*
*******************************************************************************/
#include "imu_bmi270.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "cybsp.h"
#include "mtb_hal.h"
#include "mtb_bmi270.h"

#include "i2c_bus.h"

#include "FreeRTOS.h"
#include "task.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define IMU_TASK_NAME               ("CM55 IMU Task")
#define IMU_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 8)
/* Below the GFX task, which only needs the CPU on button presses */
#define IMU_TASK_PRIORITY           (configMAX_PRIORITIES - 2)

#define ACC_RANGE_G                 (16.0f)
#define GYR_RANGE_DPS               (2000.0f)
#define RAW_FULL_SCALE              (32768.0f)

#define GYRO_BIAS_SAMPLES           (50U)     /* 1 s at 50 Hz              */
#define GYRO_BIAS_MAX_SPREAD_DPS    (3.0f)    /* reject if board moved     */

#define INIT_RETRY_DELAY_MS         (2000U)
#define I2C_LOCK_TIMEOUT_MS         (50U)

#define IMU_RSLT_ERROR              CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, \
                                        CY_RSLT_MODULE_MIDDLEWARE_BASE, 0x270U)

/* HUD layout (pixels) */
#define HUD_X                       (16)
#define HUD_Y                       (16)
#define HUD_W                       (280)
#define HUD_H                       (156)
#define HUD_PAD                     (12)
#define HUD_SCALE                   (3U)
#define HUD_LINE_H                  (26)

/*******************************************************************************
* Global Variables
*******************************************************************************/
static mtb_hal_i2c_t imu_i2c_hal;
static mtb_bmi270_t  imu_dev;
static imu_motion_t  imu_motion;

static cy_stc_scb_i2c_context_t *imu_i2c_ctx;
static hud_surface_t imu_hud;
static bool          imu_hud_enabled;

static volatile imu_state_t imu_state;

/*******************************************************************************
* Local helpers
*******************************************************************************/
static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void state_copy_out(imu_state_t *out)
{
    taskENTER_CRITICAL();
    memcpy(out, (const void *)&imu_state, sizeof(*out));
    taskEXIT_CRITICAL();
}

static cy_rslt_t sensor_init_locked(void)
{
    cy_rslt_t result;
    struct bmi2_sens_config cfg = { 0 };

    /* HAL wrapper on top of the PDL I2C that main.c already enabled */
    result = mtb_hal_i2c_setup(&imu_i2c_hal, &CYBSP_I2C_CONTROLLER_hal_config,
                               imu_i2c_ctx, NULL);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[IMU] HAL I2C setup failed: 0x%08lx\r\n", (unsigned long)result);
        return result;
    }

    /* Primary address 0x68 (SDO low), fall back to 0x69 */
    result = mtb_bmi270_init_i2c(&imu_dev, &imu_i2c_hal,
                                 MTB_BMI270_ADDRESS_DEFAULT);
    if (CY_RSLT_SUCCESS != result)
    {
        result = mtb_bmi270_init_i2c(&imu_dev, &imu_i2c_hal,
                                     MTB_BMI270_ADDRESS_SEC);
    }
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[IMU] BMI270 not found / config load failed: 0x%08lx\r\n",
               (unsigned long)result);
        return result;
    }

    /* Accel + gyro at 100 Hz ODR (driver default) ... */
    result = mtb_bmi270_config_default(&imu_dev);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[IMU] default config failed: 0x%08lx\r\n", (unsigned long)result);
        return result;
    }

    /* ... but +/-16 g so impacts are not clipped at 2 g */
    cfg.type = BMI2_ACCEL;
    if (BMI2_OK != bmi2_get_sensor_config(&cfg, 1, &imu_dev.sensor))
    {
        return IMU_RSLT_ERROR;
    }
    cfg.cfg.acc.range = BMI2_ACC_RANGE_16G;
    if (BMI2_OK != bmi2_set_sensor_config(&cfg, 1, &imu_dev.sensor))
    {
        return IMU_RSLT_ERROR;
    }

    cfg.type = BMI2_GYRO;
    if (BMI2_OK != bmi2_get_sensor_config(&cfg, 1, &imu_dev.sensor))
    {
        return IMU_RSLT_ERROR;
    }
    cfg.cfg.gyr.range = BMI2_GYR_RANGE_2000;
    if (BMI2_OK != bmi2_set_sensor_config(&cfg, 1, &imu_dev.sensor))
    {
        return IMU_RSLT_ERROR;
    }

    printf("[IMU] BMI270 ready: +/-16 g, +/-2000 dps, 100 Hz ODR\r\n");
    return CY_RSLT_SUCCESS;
}

static cy_rslt_t sensor_init(void)
{
    cy_rslt_t result;

    /* Config file upload takes a while: wait as long as needed */
    if (!i2c_bus_lock(I2C_BUS_WAIT_FOREVER))
    {
        return IMU_RSLT_ERROR;
    }
    result = sensor_init_locked();
    i2c_bus_unlock();
    return result;
}

/* Reads one sample and converts to g / dps in the goggle frame. */
static cy_rslt_t sensor_read(float acc_g[3], float gyr_dps[3])
{
    mtb_bmi270_data_t data;
    float a_raw[3];
    float g_raw[3];
    cy_rslt_t result;

    if (!i2c_bus_lock(I2C_LOCK_TIMEOUT_MS))
    {
        return IMU_RSLT_ERROR;   /* bus busy: skip this sample */
    }
    result = mtb_bmi270_read(&imu_dev, &data);
    i2c_bus_unlock();

    if (CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    a_raw[0] = (float)data.sensor_data.acc.x * (ACC_RANGE_G / RAW_FULL_SCALE);
    a_raw[1] = (float)data.sensor_data.acc.y * (ACC_RANGE_G / RAW_FULL_SCALE);
    a_raw[2] = (float)data.sensor_data.acc.z * (ACC_RANGE_G / RAW_FULL_SCALE);
    g_raw[0] = (float)data.sensor_data.gyr.x * (GYR_RANGE_DPS / RAW_FULL_SCALE);
    g_raw[1] = (float)data.sensor_data.gyr.y * (GYR_RANGE_DPS / RAW_FULL_SCALE);
    g_raw[2] = (float)data.sensor_data.gyr.z * (GYR_RANGE_DPS / RAW_FULL_SCALE);

    IMU_AXIS_REMAP(a_raw, acc_g);
    IMU_AXIS_REMAP(g_raw, gyr_dps);
    return CY_RSLT_SUCCESS;
}

/* Averages the gyro for ~1 s. Keeps zero bias if the board was moving. */
static void gyro_bias_calibrate(float bias[3])
{
    float sum[3] = { 0.0f, 0.0f, 0.0f };
    float min[3] = {  1e9f,  1e9f,  1e9f };
    float max[3] = { -1e9f, -1e9f, -1e9f };
    uint32_t n = 0U;
    uint32_t i;
    uint32_t k;
    TickType_t wake = xTaskGetTickCount();

    bias[0] = 0.0f;
    bias[1] = 0.0f;
    bias[2] = 0.0f;

    for (i = 0U; i < GYRO_BIAS_SAMPLES; i++)
    {
        float a[3];
        float g[3];
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
        if (CY_RSLT_SUCCESS != sensor_read(a, g))
        {
            continue;
        }
        for (k = 0U; k < 3U; k++)
        {
            sum[k] += g[k];
            min[k] = (g[k] < min[k]) ? g[k] : min[k];
            max[k] = (g[k] > max[k]) ? g[k] : max[k];
        }
        n++;
    }

    if (n < (GYRO_BIAS_SAMPLES / 2U))
    {
        printf("[IMU] gyro bias: too few samples, using 0\r\n");
        return;
    }
    for (k = 0U; k < 3U; k++)
    {
        if ((max[k] - min[k]) > GYRO_BIAS_MAX_SPREAD_DPS)
        {
            printf("[IMU] gyro bias: board moving, using 0 (keep still at boot)\r\n");
            return;
        }
    }
    for (k = 0U; k < 3U; k++)
    {
        bias[k] = sum[k] / (float)n;
    }
}

static void hud_line(int32_t line, const char *label, const char *value,
                     uint16_t color)
{
    char text[24];
    int32_t y = HUD_Y + HUD_PAD + (line * HUD_LINE_H);

    (void)snprintf(text, sizeof(text), "%-6s%s", label, value);
    /* Pad to a fixed width so shorter strings erase older, longer ones */
    {
        size_t len = strlen(text);
        while (len < 14U)
        {
            text[len++] = ' ';
        }
        text[len] = '\0';
    }
    (void)hud_draw_text(&imu_hud, HUD_X + HUD_PAD, y, text, HUD_SCALE,
                        color, HUD_COLOR_PANEL);
}

static void hud_flush(void)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    /* Push the panel rows from D-cache to memory so the display controller
     * (bus master) sees them. */
    uint16_t *start = &imu_hud.pixels[(uint32_t)HUD_Y * imu_hud.stride];
    int32_t   bytes = (int32_t)((uint32_t)HUD_H * imu_hud.stride * sizeof(uint16_t));
    SCB_CleanDCache_by_Addr((void *)start, bytes);
#endif
}

static void hud_draw(const imu_state_t *st, bool sensor_ok)
{
    char value[16];
    uint32_t now = now_ms();

    if (!imu_hud_enabled)
    {
        return;
    }

    hud_line(0, "BMI270", "", HUD_COLOR_GREY);

    if (!sensor_ok || !st->valid)
    {
        hud_line(1, "PITCH", "   ---", HUD_COLOR_GREY);
        hud_line(2, "ROLL",  "   ---", HUD_COLOR_GREY);
        hud_line(3, "ACC G", "   ---", HUD_COLOR_GREY);
        hud_line(4, sensor_ok ? "INIT" : "NO IMU", "", HUD_COLOR_RED);
        hud_flush();
        return;
    }

    hud_format_fixed1(value, sizeof(value), st->pitch_deg, 6U);
    hud_line(1, "PITCH", value, HUD_COLOR_WHITE);
    hud_format_fixed1(value, sizeof(value), st->roll_deg, 6U);
    hud_line(2, "ROLL", value, HUD_COLOR_WHITE);
    hud_format_fixed1(value, sizeof(value), st->accel_norm_g, 6U);
    hud_line(3, "ACC G", value, HUD_COLOR_WHITE);

    if ((0U != st->last_event) &&
        ((uint32_t)(now - st->last_event_ms) < IMU_EVENT_HOLD_MS))
    {
        if (0U != (st->last_event & IMU_EVENT_FALL))
        {
            hud_line(4, "FALL!", "", HUD_COLOR_RED);
        }
        else if (0U != (st->last_event & IMU_EVENT_IMPACT))
        {
            char v[16];
            hud_format_fixed1(v, sizeof(v), st->last_impact_g, 5U);
            (void)snprintf(value, sizeof(value), "%sG", v);
            hud_line(4, "IMPACT", value, HUD_COLOR_YELLOW);
        }
        else
        {
            hud_line(4, "FREE", "FALL", HUD_COLOR_YELLOW);
        }
    }
    else
    {
        hud_line(4, "OK", "", HUD_COLOR_GREEN);
    }
    hud_flush();
}

static void log_state(const imu_state_t *st)
{
    char p[12];
    char r[12];
    char a[12];

    hud_format_fixed1(p, sizeof(p), st->pitch_deg, 0U);
    hud_format_fixed1(r, sizeof(r), st->roll_deg, 0U);
    hud_format_fixed1(a, sizeof(a), st->accel_norm_g, 0U);
    printf("[IMU] pitch %s  roll %s  |a| %s g\r\n", p, r, a);
}

static void log_events(uint32_t ev, float impact_g)
{
    char v[12];

    if (0U != (ev & IMU_EVENT_FREEFALL))
    {
        printf("[IMU] event: FREE FALL\r\n");
    }
    if (0U != (ev & IMU_EVENT_IMPACT))
    {
        hud_format_fixed1(v, sizeof(v), impact_g, 0U);
        printf("[IMU] event: IMPACT (>= %s g)\r\n", v);
    }
    if (0U != (ev & IMU_EVENT_FALL))
    {
        printf("[IMU] event: FALL detected\r\n");
    }
}

/*******************************************************************************
* Function Name: imu_task
********************************************************************************
* Summary:
*   Initialises the BMI270, calibrates gyro bias, then runs the 50 Hz loop:
*   read -> filter -> detect -> publish state -> HUD / log.
*******************************************************************************/
static void imu_task(void *arg)
{
    float bias[3] = { 0.0f, 0.0f, 0.0f };
    TickType_t wake;
    uint32_t last_ms;
    uint32_t last_hud_ms = 0U;
    uint32_t last_log_ms = 0U;
    imu_state_t snap;

    CY_UNUSED_PARAMETER(arg);

    if (imu_hud_enabled)
    {
        hud_fill_rect(&imu_hud, HUD_X, HUD_Y, HUD_W, HUD_H, HUD_COLOR_PANEL);
    }
    memset(&snap, 0, sizeof(snap));
    hud_draw(&snap, true);

    while (CY_RSLT_SUCCESS != sensor_init())
    {
        hud_draw(&snap, false);
        vTaskDelay(pdMS_TO_TICKS(INIT_RETRY_DELAY_MS));
    }

    gyro_bias_calibrate(bias);
    {
        char b[3][12];
        hud_format_fixed1(b[0], sizeof(b[0]), bias[0], 0U);
        hud_format_fixed1(b[1], sizeof(b[1]), bias[1], 0U);
        hud_format_fixed1(b[2], sizeof(b[2]), bias[2], 0U);
        printf("[IMU] gyro bias dps: %s %s %s\r\n", b[0], b[1], b[2]);
    }

    imu_motion_init(&imu_motion, NULL);
    wake    = xTaskGetTickCount();
    last_ms = now_ms();

    for (;;)
    {
        float a[3];
        float g[3];
        uint32_t t;
        uint32_t ev;
        float dt_s;

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));

        if (CY_RSLT_SUCCESS != sensor_read(a, g))
        {
            continue;   /* transient bus error: skip this sample */
        }

        g[0] -= bias[0];
        g[1] -= bias[1];
        g[2] -= bias[2];

        t       = now_ms();
        dt_s    = (float)(t - last_ms) * 0.001f;
        last_ms = t;

        ev = imu_motion_update(&imu_motion, a, g, dt_s, t);

        taskENTER_CRITICAL();
        imu_state.timestamp_ms = t;
        imu_state.accel_g[0]   = a[0];
        imu_state.accel_g[1]   = a[1];
        imu_state.accel_g[2]   = a[2];
        imu_state.gyro_dps[0]  = g[0];
        imu_state.gyro_dps[1]  = g[1];
        imu_state.gyro_dps[2]  = g[2];
        imu_state.accel_norm_g = imu_motion.accel_norm_g;
        imu_state.pitch_deg    = imu_motion.pitch_deg;
        imu_state.roll_deg     = imu_motion.roll_deg;
        imu_state.valid        = imu_motion.attitude_valid;
        imu_state.last_impact_g = imu_motion_last_impact_g(&imu_motion);
        if (IMU_EVENT_NONE != ev)
        {
            imu_state.events_latched |= ev;
            /* Highest severity wins for display */
            imu_state.last_event = (0U != (ev & IMU_EVENT_FALL))   ? IMU_EVENT_FALL :
                                   (0U != (ev & IMU_EVENT_IMPACT)) ? IMU_EVENT_IMPACT :
                                                                     IMU_EVENT_FREEFALL;
            imu_state.last_event_ms = t;
        }
        taskEXIT_CRITICAL();

        if (IMU_EVENT_NONE != ev)
        {
            log_events(ev, imu_motion_last_impact_g(&imu_motion));
        }

        if ((uint32_t)(t - last_hud_ms) >= IMU_HUD_PERIOD_MS)
        {
            last_hud_ms = t;
            state_copy_out(&snap);
            hud_draw(&snap, true);
        }

        if ((0U != IMU_LOG_PERIOD_MS) &&
            ((uint32_t)(t - last_log_ms) >= IMU_LOG_PERIOD_MS))
        {
            last_log_ms = t;
            state_copy_out(&snap);
            if (snap.valid)
            {
                log_state(&snap);
            }
        }
    }
}

/*******************************************************************************
* Public API
*******************************************************************************/
cy_rslt_t imu_bmi270_start(cy_stc_scb_i2c_context_t *i2c_ctx,
                           const hud_surface_t *hud)
{
    BaseType_t ok;

    if (NULL == i2c_ctx)
    {
        return IMU_RSLT_ERROR;
    }
    imu_i2c_ctx = i2c_ctx;

    imu_hud_enabled = (NULL != hud) && (NULL != hud->pixels);
    if (imu_hud_enabled)
    {
        imu_hud = *hud;
    }

    ok = xTaskCreate(imu_task, IMU_TASK_NAME, IMU_TASK_STACK_SIZE, NULL,
                     IMU_TASK_PRIORITY, NULL);
    return (pdPASS == ok) ? CY_RSLT_SUCCESS : IMU_RSLT_ERROR;
}

bool imu_get_state(imu_state_t *out)
{
    if (NULL == out)
    {
        return false;
    }
    state_copy_out(out);
    return out->valid;
}

bool imu_get_attitude(float *pitch_deg, float *roll_deg)
{
    bool valid;

    taskENTER_CRITICAL();
    valid = imu_state.valid;
    if (NULL != pitch_deg)
    {
        *pitch_deg = imu_state.pitch_deg;
    }
    if (NULL != roll_deg)
    {
        *roll_deg = imu_state.roll_deg;
    }
    taskEXIT_CRITICAL();
    return valid;
}

uint32_t imu_take_events(void)
{
    uint32_t ev;

    taskENTER_CRITICAL();
    ev = imu_state.events_latched;
    imu_state.events_latched = IMU_EVENT_NONE;
    taskEXIT_CRITICAL();
    return ev;
}
