/*******************************************************************************
* File Name        : sht40_env.c
*
* Description      : See sht40_env.h.
*
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "sht40_env.h"
#include "sht40_config.h"
#include "sht40_i2c_hal_psoc.h"

#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"
#include "sht4x_i2c.h"

#include "task.h"
#include "semphr.h"

#define SHT40_TASK_NAME             ("SHT40 Task")
/* Stack in words: float math plus a few printf calls at start-up */
#define SHT40_TASK_STACK_SIZE       (configMINIMAL_STACK_SIZE * 4)

#define SHT40_SOFT_RESET_DELAY_US   (1000U)     /* datasheet: 1 ms max */
#define SHT40_POWER_UP_DELAY_US     (1000U)     /* datasheet: 1 ms max */


/*******************************************************************************
* Static data
*******************************************************************************/
static sht40_state_t env_state;
static SemaphoreHandle_t env_mutex = NULL;
static StaticSemaphore_t env_mutex_storage;
static TaskHandle_t env_task = NULL;


/*******************************************************************************
* Weak porting hooks
*******************************************************************************/
__attribute__((weak)) bool sht40_port_get_lens_temp_c(float* lens_temp_c)
{
    (void)lens_temp_c;
    return false;
}

__attribute__((weak)) bool sht40_port_get_battery_temp_c(float* batt_temp_c)
{
    (void)batt_temp_c;
    return false;
}

__attribute__((weak)) void sht40_port_battery_heater(bool on)
{
    (void)on;
}

__attribute__((weak)) void sht40_port_fog_level_changed(sht40_fog_level_t level)
{
    (void)level;
}


/*******************************************************************************
* Helpers
*******************************************************************************/
const char* sht40_fog_level_str(sht40_fog_level_t level)
{
    switch (level)
    {
        case SHT40_FOG_OK:     return "OK";
        case SHT40_FOG_RISK:   return "RISK";
        case SHT40_FOG_LIKELY: return "FOG";
        default:               return "?";
    }
}

const char* sht40_status_str(sht40_status_t status)
{
    switch (status)
    {
        case SHT40_STATUS_INIT:      return "INIT";
        case SHT40_STATUS_OK:        return "OK";
        case SHT40_STATUS_HEATING:   return "HEATING";
        case SHT40_STATUS_NO_SENSOR: return "NO SENSOR";
        case SHT40_STATUS_ERROR:     return "ERROR";
        default:                     return "?";
    }
}

static void state_lock(void)
{
    (void)xSemaphoreTake(env_mutex, portMAX_DELAY);
}

static void state_unlock(void)
{
    (void)xSemaphoreGive(env_mutex);
}

static void set_status(sht40_status_t status)
{
    state_lock();
    env_state.status = status;
    state_unlock();
}

void sht40_env_get_state(sht40_state_t* state)
{
    if (NULL == state)
    {
        return;
    }
    if (NULL == env_mutex)
    {
        memset(state, 0, sizeof(*state));
        state->status = SHT40_STATUS_INIT;
        return;
    }
    state_lock();
    *state = env_state;
    state_unlock();
}


/* Soft reset + serial number read: confirms the sensor answers with valid CRC */
static bool sht40_probe(uint32_t* serial)
{
    int16_t err;

    sensirion_i2c_hal_sleep_usec(SHT40_POWER_UP_DELAY_US);

    /* A NAK here is not fatal (sensor may still be busy): read the serial */
    (void)sht4x_soft_reset();
    sensirion_i2c_hal_sleep_usec(SHT40_SOFT_RESET_DELAY_US);

    err = sht4x_serial_number(serial);
    return (NO_ERROR == err);
}


/* Derives dew point, fog level and heater request from the filtered values.
 * Called with the new sample already stored in 'st' (local copy). */
static void derive(sht40_state_t* st)
{
    float lens_t;
    float batt_t;
    float condensation_point;

    st->dew_point_c      = sht40_dew_point_c(st->temperature_c, st->humidity_pct);
    st->frost_point_c    = sht40_frost_point_c(st->temperature_c, st->humidity_pct);
    st->abs_humidity_gm3 = sht40_abs_humidity_gm3(st->temperature_c, st->humidity_pct);

    if (!sht40_port_get_lens_temp_c(&lens_t))
    {
        lens_t = st->temperature_c - SHT40_LENS_DELTA_C;
    }
    st->lens_temp_c = lens_t;

    /* Below 0 C on the lens, ice (frost) forms at the frost point, which is
     * higher than the dew point: use it so the warning is not late. */
    condensation_point = (lens_t < 0.0f) ? st->frost_point_c : st->dew_point_c;
    st->fog_margin_c = lens_t - condensation_point;
    st->fog_level = sht40_fog_update(st->fog_level, st->fog_margin_c,
                                     SHT40_FOG_RISK_ON_C, SHT40_FOG_ON_C,
                                     SHT40_FOG_HYST_C);

    if (!sht40_port_get_battery_temp_c(&batt_t))
    {
        batt_t = st->temperature_c;
    }
    st->batt_temp_c = batt_t;
    st->batt_heater_on = sht40_batt_heater_update(st->batt_heater_on, batt_t,
                                                  SHT40_BATT_HEAT_ON_C,
                                                  SHT40_BATT_HEAT_OFF_C);
}


/*******************************************************************************
* Function Name: sht40_task
********************************************************************************
* Summary:
*  Probes the sensor, then measures T / RH every SHT40_SAMPLE_PERIOD_MS with
*  high repeatability, filters, derives the fog / heater outputs and runs the
*  optional on-chip heater for condensation recovery.
*******************************************************************************/
static void sht40_task(void* arg)
{
    sht40_state_t st;
    uint32_t serial = 0U;
    uint32_t consecutive_err = 0U;
    uint32_t creep_s = 0U;
    uint32_t since_heater_s = SHT40_HEATER_MIN_INTERVAL_S;
    uint32_t settle_s = 0U;
    bool first = true;
    TickType_t last_wake;
    const uint32_t period_s = (SHT40_SAMPLE_PERIOD_MS + 999U) / 1000U;

    (void)arg;

    sensirion_i2c_hal_init();
    sht4x_init(SHT40_I2C_ADDR);

    if (!sht40_probe(&serial))
    {
        printf("SHT40: no response at 0x%02X, humidity overlay disabled\r\n",
               (unsigned int)SHT40_I2C_ADDR);
        set_status(SHT40_STATUS_NO_SENSOR);
        vTaskDelete(NULL);
        return;
    }

    printf("SHT40: serial 0x%08lX at 0x%02X\r\n", (unsigned long)serial,
           (unsigned int)SHT40_I2C_ADDR);

    state_lock();
    env_state.serial_number = serial;
    st = env_state;
    state_unlock();

    last_wake = xTaskGetTickCount();

    for (;;)
    {
        int32_t t_milli = 0;
        int32_t rh_milli = 0;
        int16_t err;

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SHT40_SAMPLE_PERIOD_MS));

        err = sht4x_measure_high_precision(&t_milli, &rh_milli);

        if (NO_ERROR != err)
        {
            consecutive_err++;
            state_lock();
            env_state.error_count++;
            if (consecutive_err >= SHT40_MAX_ERRORS)
            {
                env_state.status = SHT40_STATUS_ERROR;
            }
            state_unlock();
            continue;
        }
        consecutive_err = 0U;

        if (since_heater_s < SHT40_HEATER_MIN_INTERVAL_S)
        {
            since_heater_s += period_s;
        }

        /* Discard readings while the sensor cools after a heater pulse */
        if (settle_s > 0U)
        {
            settle_s = (settle_s > period_s) ? (settle_s - period_s) : 0U;
            if (0U == settle_s)
            {
                set_status(SHT40_STATUS_OK);
            }
            continue;
        }

        {
            float t = (float)t_milli / 1000.0f;
            float rh = sht40_clamp_rh((float)rh_milli / 1000.0f);
            sht40_fog_level_t prev_fog;
            bool prev_heat;

            state_lock();
            st = env_state;
            state_unlock();

            prev_fog = st.fog_level;
            prev_heat = st.batt_heater_on;

            if (first)
            {
                st.temperature_c = t;
                st.humidity_pct = rh;
                first = false;
            }
            else
            {
                st.temperature_c += SHT40_FILTER_ALPHA * (t - st.temperature_c);
                st.humidity_pct  += SHT40_FILTER_ALPHA * (rh - st.humidity_pct);
            }

            derive(&st);
            st.sample_count++;
            st.status = SHT40_STATUS_OK;

            state_lock();
            /* error_count may have been updated meanwhile: keep it */
            st.error_count = env_state.error_count;
            env_state = st;
            state_unlock();

            if (st.fog_level != prev_fog)
            {
                sht40_port_fog_level_changed(st.fog_level);
            }
            if (st.batt_heater_on != prev_heat)
            {
                sht40_port_battery_heater(st.batt_heater_on);
            }

            /* Condensation / creep recovery with the on-chip heater */
            creep_s = (rh >= SHT40_CREEP_RH_PCT) ? (creep_s + period_s) : 0U;

#if (SHT40_AUTO_HEATER != 0)
            if ((creep_s >= SHT40_CREEP_TIME_S) &&
                (since_heater_s >= SHT40_HEATER_MIN_INTERVAL_S))
            {
                int32_t ht = 0;
                int32_t hrh = 0;

                set_status(SHT40_STATUS_HEATING);
                /* 110 mW for 0.1 s; the returned (heated) sample is ignored */
                (void)sht4x_activate_medium_heater_power_short(&ht, &hrh);
                since_heater_s = 0U;
                creep_s = 0U;
                settle_s = SHT40_HEATER_SETTLE_S;
            }
#endif
        }
    }
}


cy_rslt_t sht40_env_start(CySCB_Type* base, cy_stc_scb_i2c_context_t* context,
                          UBaseType_t priority)
{
    cy_rslt_t rslt;

    if (NULL != env_task)
    {
        return CY_RSLT_SUCCESS;
    }

    if (NULL == env_mutex)
    {
        env_mutex = xSemaphoreCreateMutexStatic(&env_mutex_storage);
        if (NULL == env_mutex)
        {
            return SHT40_ENV_RSLT_ERR;
        }
    }

    memset(&env_state, 0, sizeof(env_state));
    env_state.status = SHT40_STATUS_INIT;
    env_state.fog_level = SHT40_FOG_OK;

    rslt = sht40_i2c_hal_bind(base, context);
    if (CY_RSLT_SUCCESS != rslt)
    {
        return rslt;
    }

    if (pdPASS != xTaskCreate(sht40_task, SHT40_TASK_NAME, SHT40_TASK_STACK_SIZE,
                              NULL, priority, &env_task))
    {
        env_task = NULL;
        return SHT40_ENV_RSLT_ERR;
    }

    return CY_RSLT_SUCCESS;
}

/* [] END OF FILE */
