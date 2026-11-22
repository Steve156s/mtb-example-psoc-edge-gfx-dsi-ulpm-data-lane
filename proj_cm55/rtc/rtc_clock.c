/*******************************************************************************
* File Name        : rtc_clock.c
*
* Description      : HUD goggle clock on the PSOC Edge E84 RTC (WCO 32.768 kHz).
*                    See rtc_clock.h.
*
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "cybsp.h"
#include "cy_rtc.h"
#include "cy_sysclk.h"
#include "cy_sysint.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "rtc_clock.h"

#define RTC_RETRY_DELAY_MS      (5U)

static SemaphoreHandle_t rtc_mutex;
static SemaphoreHandle_t minute_sem;

static rtc_src_t   cur_src = RTC_SRC_NONE;
static int64_t     last_sync_unix;      /* RTC UTC at the last accepted sync */
static int16_t     cur_tz_min = RTC_DEFAULT_TZ_MIN;
static int16_t     cur_calib_ppm_x10;
static bool        wco_ok;
static bool        bak_is_wco;
static volatile uint32_t minute_ticks;

static const char * const src_names[] =
{
    "NONE", "KEPT", "UART", "PHONE", "GNSS"
};


/*******************************************************************************
* RTC ALARM1 interrupt (srss_interrupt_rtc). Fires at second 00 of every minute,
* also in Deep Sleep, and wakes the CM55.
*******************************************************************************/
static void rtc_backup_isr(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t status = Cy_RTC_GetInterruptStatusMasked();

    Cy_RTC_ClearInterrupt(status);

    if (0U != (status & CY_RTC_INTR_ALARM1))
    {
        minute_ticks++;
        if (NULL != minute_sem)
        {
            (void)xSemaphoreGiveFromISR(minute_sem, &woken);
        }
    }
    portYIELD_FROM_ISR(woken);
}


/* Reads the RTC as UTC. Must be called with rtc_mutex held. */
static void read_rtc_utc(rtc_datetime_t *utc)
{
    cy_stc_rtc_config_t now;

    Cy_RTC_GetDateAndTime(&now);

    utc->year  = (int16_t)(RTC_TIME_YEAR_BASE + (int)now.year);
    utc->month = (uint8_t)now.month;
    utc->day   = (uint8_t)now.date;
    utc->min   = (uint8_t)now.min;
    utc->sec   = (uint8_t)now.sec;
    utc->wday  = (uint8_t)now.dayOfWeek;

    if (CY_RTC_12_HOURS == now.hrFormat)
    {
        uint32_t h = now.hour % 12U;
        utc->hour = (uint8_t)((CY_RTC_PM == now.amPm) ? (h + 12U) : h);
    }
    else
    {
        utc->hour = (uint8_t)now.hour;
    }
}


/* Writes UTC to the RTC with retries (the RTC answers CY_RTC_INVALID_STATE
 * while it synchronises its registers). Must be called with rtc_mutex held. */
static bool write_rtc_utc(const rtc_datetime_t *utc)
{
    cy_en_rtc_status_t st = CY_RTC_INVALID_STATE;
    uint32_t retry = RTC_ACCESS_RETRY_COUNT;

    if (!rtc_time_is_valid(utc))
    {
        return false;
    }

    /* keep the RTC in 24 hour mode so the alarm hour matches UTC hours */
    (void)Cy_RTC_SetHoursFormat(CY_RTC_24_HOURS);

    do
    {
        st = Cy_RTC_SetDateAndTimeDirect(utc->sec, utc->min, utc->hour,
                                         utc->day, utc->month,
                                         (uint32_t)(utc->year - RTC_TIME_YEAR_BASE));
        if (CY_RTC_SUCCESS != st)
        {
            vTaskDelay(pdMS_TO_TICKS(RTC_RETRY_DELAY_MS));
        }
    } while ((CY_RTC_SUCCESS != st) && (0U != --retry));

    return (CY_RTC_SUCCESS == st);
}


/* ALARM1: second == 0, every other field "don't care" -> once a minute */
static bool enable_minute_alarm(void)
{
    const cy_stc_rtc_alarm_t alarm =
    {
        .sec = 0U,          .secEn       = CY_RTC_ALARM_ENABLE,
        .min = 0U,          .minEn       = CY_RTC_ALARM_DISABLE,
        .hour = 0U,         .hourEn      = CY_RTC_ALARM_DISABLE,
        .dayOfWeek = 1U,    .dayOfWeekEn = CY_RTC_ALARM_DISABLE,
        .date = 1U,         .dateEn      = CY_RTC_ALARM_DISABLE,
        .month = 1U,        .monthEn     = CY_RTC_ALARM_DISABLE,
        .almEn = CY_RTC_ALARM_ENABLE
    };
    const cy_stc_sysint_t irq_cfg =
    {
        .intrSrc      = RTC_IRQN,
        .intrPriority = RTC_IRQ_PRIORITY
    };
    cy_en_rtc_status_t st;
    uint32_t retry = RTC_ACCESS_RETRY_COUNT;

    do
    {
        st = Cy_RTC_SetAlarmDateAndTime(&alarm, CY_RTC_ALARM_1);
        if (CY_RTC_SUCCESS != st)
        {
            vTaskDelay(pdMS_TO_TICKS(RTC_RETRY_DELAY_MS));
        }
    } while ((CY_RTC_SUCCESS != st) && (0U != --retry));

    if (CY_RTC_SUCCESS != st)
    {
        return false;
    }

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&irq_cfg, rtc_backup_isr))
    {
        return false;
    }
    Cy_RTC_ClearInterrupt(CY_RTC_INTR_ALARM1);
    NVIC_ClearPendingIRQ(irq_cfg.intrSrc);
    Cy_RTC_SetInterruptMask(Cy_RTC_GetInterruptMask() | CY_RTC_INTR_ALARM1);
    NVIC_EnableIRQ(irq_cfg.intrSrc);
    return true;
}


cy_rslt_t rtc_clock_init(void)
{
    rtc_datetime_t utc;

    rtc_mutex  = xSemaphoreCreateMutex();
    minute_sem = xSemaphoreCreateBinary();
    if ((NULL == rtc_mutex) || (NULL == minute_sem))
    {
        return CY_RSLT_TYPE_ERROR;
    }

    /* The BSP (design.modus) enables the WCO and selects it as the backup
     * domain clock (CLK_BAK = WCO) that drives the RTC. Only report here: the
     * clock tree belongs to the CM33 side and must not be changed at run time
     * by the CM55. */
    wco_ok     = Cy_SysClk_WcoOkay();
    bak_is_wco = (CY_SYSCLK_BAK_IN_WCO == Cy_SysClk_ClkBakGetSource());

    (void)xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    read_rtc_utc(&utc);
    if (utc.year >= RTC_VALID_MIN_YEAR)
    {
        /* time survived the reset in the backup domain */
        cur_src = RTC_SRC_KEPT;
        last_sync_unix = rtc_time_to_unix(&utc);
    }
    (void)xSemaphoreGive(rtc_mutex);

    printf("RTC: WCO %s, RTC clock %s, time %s\r\n",
           wco_ok ? "OK" : "NOT RUNNING",
           bak_is_wco ? "WCO 32.768 kHz" : "NOT WCO (check design.modus)",
           (RTC_SRC_KEPT == cur_src) ? "kept from before reset" : "not set");

    if (!enable_minute_alarm())
    {
        printf("RTC: minute alarm setup failed\r\n");
        return CY_RSLT_TYPE_ERROR;
    }
    return CY_RSLT_SUCCESS;
}


void rtc_clock_get_state(rtc_clock_state_t *st)
{
    memset(st, 0, sizeof(*st));

    (void)xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    read_rtc_utc(&st->utc);
    st->src           = cur_src;
    st->tz_min        = cur_tz_min;
    st->calib_ppm_x10 = cur_calib_ppm_x10;
    st->unix_utc      = rtc_time_to_unix(&st->utc);
    st->sync_age_s    = (st->unix_utc > last_sync_unix) ?
                        (uint32_t)(st->unix_utc - last_sync_unix) : 0U;
    (void)xSemaphoreGive(rtc_mutex);

    st->time_valid   = (st->utc.year >= RTC_VALID_MIN_YEAR);
    st->wco_ok       = wco_ok;
    st->bak_is_wco   = bak_is_wco;
    st->minute_ticks = minute_ticks;

    st->local = st->utc;
    rtc_time_add_seconds(&st->local, (int32_t)st->tz_min * 60);
}


bool rtc_clock_sync(rtc_src_t src, int64_t unix_utc)
{
    rtc_datetime_t now, target;
    int64_t now_unix, diff;
    bool accept, written = false;

    if ((src <= RTC_SRC_KEPT) || (src > RTC_SRC_GNSS))
    {
        return false;
    }
    rtc_time_from_unix(unix_utc, &target);
    if (!rtc_time_is_valid(&target) || (target.year < RTC_VALID_MIN_YEAR))
    {
        return false;
    }

    (void)xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    read_rtc_utc(&now);
    now_unix = rtc_time_to_unix(&now);

    accept = (RTC_SRC_UART == src) || (src >= cur_src) ||
             ((now_unix - last_sync_unix) > (int64_t)RTC_SYNC_STALE_S);

    if (accept)
    {
        diff = unix_utc - now_unix;
        if ((RTC_SRC_UART == src) || (now.year < RTC_VALID_MIN_YEAR) ||
            (diff >= RTC_SYNC_MIN_STEP_S) || (diff <= -RTC_SYNC_MIN_STEP_S))
        {
            written = write_rtc_utc(&target);
        }
        else
        {
            written = true;     /* already within RTC_SYNC_MIN_STEP_S */
        }
        if (written)
        {
            cur_src = src;
            last_sync_unix = unix_utc;
        }
    }
    (void)xSemaphoreGive(rtc_mutex);

    printf("RTC: sync from %s %s (step %ld s)\r\n", rtc_clock_src_name(src),
           !accept ? "ignored (lower quality)" : (written ? "accepted" : "FAILED"),
           (long)(unix_utc - now_unix));
    return written && accept;
}


bool rtc_clock_set_local(rtc_src_t src, const rtc_datetime_t *local)
{
    int16_t tz;

    if (!rtc_time_is_valid(local))
    {
        return false;
    }
    (void)xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    tz = cur_tz_min;
    (void)xSemaphoreGive(rtc_mutex);

    return rtc_clock_sync(src, rtc_time_to_unix(local) - ((int64_t)tz * 60));
}


void rtc_clock_set_tz(int16_t tz_min)
{
    if ((tz_min < RTC_TIME_TZ_MIN_MINUTES) || (tz_min > RTC_TIME_TZ_MAX_MINUTES))
    {
        return;
    }
    (void)xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    cur_tz_min = tz_min;
    (void)xSemaphoreGive(rtc_mutex);
}


bool rtc_clock_set_calibration(int16_t ppm_x10)
{
    bool add = false;
    uint8_t step = rtc_time_ppm_to_calib(ppm_x10, &add);
    cy_en_rtc_status_t st;

    (void)xSemaphoreTake(rtc_mutex, portMAX_DELAY);
    if (0U == step)
    {
        st = Cy_RTC_CalibrationControlDisable();
    }
    else
    {
        st = Cy_RTC_CalibrationControlEnable(step,
                 add ? CY_RTC_CALIB_SIGN_POSITIVE : CY_RTC_CALIB_SIGN_NEGATIVE,
                 CY_RTC_CAL_SEL_CAL1);
    }
    if (CY_RTC_SUCCESS == st)
    {
        cur_calib_ppm_x10 = ppm_x10;
    }
    (void)xSemaphoreGive(rtc_mutex);
    return (CY_RTC_SUCCESS == st);
}


bool rtc_clock_wait_minute(uint32_t timeout_ms)
{
    TickType_t ticks = (RTC_WAIT_FOREVER == timeout_ms) ? portMAX_DELAY :
                       pdMS_TO_TICKS(timeout_ms);

    return (pdTRUE == xSemaphoreTake(minute_sem, ticks));
}


void rtc_clock_wake_waiter(void)
{
    (void)xSemaphoreGive(minute_sem);
}


const char *rtc_clock_src_name(rtc_src_t src)
{
    return ((uint32_t)src < (sizeof(src_names) / sizeof(src_names[0]))) ?
           src_names[src] : "?";
}


bool rtc_clock_sync_from_gnss(int64_t unix_utc)
{
    return rtc_clock_sync(RTC_SRC_GNSS, unix_utc);
}


bool rtc_clock_sync_from_phone(int64_t unix_utc, int16_t tz_min)
{
    rtc_clock_set_tz(tz_min);
    return rtc_clock_sync(RTC_SRC_PHONE, unix_utc);
}
