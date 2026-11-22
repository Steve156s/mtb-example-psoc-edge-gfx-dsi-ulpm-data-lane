/*******************************************************************************
* File Name        : rtc_clock.h
*
* Description      : HUD goggle clock on the PSOC Edge E84 RTC.
*                    - The RTC runs from the 32.768 kHz WCO crystal (P18.0
*                      WCO_OUT / P18.1 WCO_IN) through the backup domain clock
*                      (BSP: CLK_BAK = WCO), so it keeps counting in Deep
*                      Sleep and Hibernate and across a warm reset.
*                    - The RTC holds UTC. The HUD shows local time = UTC + the
*                      time zone offset.
*                    - Time sources: UART command now; GNSS (Tracker over
*                      Bluetooth) and the phone App later, through
*                      rtc_clock_sync(). A better source wins (GNSS > phone >
*                      UART), a manual UART set always wins.
*                    - RTC ALARM1 fires at second 00 of every minute. It wakes
*                      the CM55 from Deep Sleep and releases
*                      rtc_clock_wait_minute() for the low-power clock mode.
*
*                    The RTC is initialised by the CM33 non-secure project
*                    (proj_cm33_ns/main.c, setup_clib_support()); it only
*                    loads the BSP default time when the RTC was never set.
*
* Related Document : See README.md, section "RTC clock (WCO 32.768 kHz)"
*
*******************************************************************************/
#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "cy_result.h"
#include "rtc_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
* Configuration (override with DEFINES+=... in proj_cm55/Makefile)
*******************************************************************************/
/* Default time zone in minutes east of UTC (Taiwan = +8:00) */
#ifndef RTC_DEFAULT_TZ_MIN
#define RTC_DEFAULT_TZ_MIN              (480)
#endif

/* The RTC counts as "set" only from this year on. The BSP default date in
 * design.modus is 2024-01-01 12:00, so any 2024 date means "never set". */
#ifndef RTC_VALID_MIN_YEAR
#define RTC_VALID_MIN_YEAR              (2025)
#endif

/* After this many seconds without a sync, any source may set the clock
 * again (e.g. phone after the GNSS fix was lost for a day). */
#ifndef RTC_SYNC_STALE_S
#define RTC_SYNC_STALE_S                (6UL * 3600UL)
#endif

/* A sync that differs less than this from the RTC is only logged, the RTC is
 * not rewritten (avoids a visible jump of the seconds every GNSS fix). */
#ifndef RTC_SYNC_MIN_STEP_S
#define RTC_SYNC_MIN_STEP_S             (2)
#endif

/* RTC interrupt line. On PSOC Edge E84 this is srss_interrupt_rtc_IRQn (as in
 * the Infineon "RTC periodic wakeup" example, there on the CM33). Only one
 * core may own it: the CM33 projects of this example do not enable it. */
#ifndef RTC_IRQN
#define RTC_IRQN                        (srss_interrupt_rtc_IRQn)
#endif

/* RTC interrupt priority. Must be numerically >= the
 * FreeRTOS configMAX_SYSCALL_INTERRUPT_PRIORITY level (1 with 3 bits). */
#ifndef RTC_IRQ_PRIORITY
#define RTC_IRQ_PRIORITY                (4U)
#endif

/* Number of 5 ms retries when the RTC is busy synchronising its registers */
#ifndef RTC_ACCESS_RETRY_COUNT
#define RTC_ACCESS_RETRY_COUNT          (200U)
#endif

/* Timeout for rtc_clock_wait_minute(): wait without a timeout */
#define RTC_WAIT_FOREVER                (0xFFFFFFFFUL)

/*******************************************************************************
* Types
*******************************************************************************/
/* Time sources, ordered by quality (higher wins) */
typedef enum
{
    RTC_SRC_NONE = 0,   /* never set: BSP default time                    */
    RTC_SRC_KEPT,       /* set before this boot, kept by the backup domain */
    RTC_SRC_UART,       /* manual UART "T"/"U" command                     */
    RTC_SRC_PHONE,      /* phone App over BLE (future)                     */
    RTC_SRC_GNSS        /* Tracker GNSS time over Bluetooth (future)       */
} rtc_src_t;

typedef struct
{
    bool           time_valid;  /* RTC holds a real time (>= RTC_VALID_MIN_YEAR) */
    bool           wco_ok;      /* WCO crystal running                           */
    bool           bak_is_wco;  /* backup domain (RTC) clock is the WCO           */
    rtc_src_t      src;         /* source of the last accepted sync              */
    int64_t        unix_utc;    /* current UTC, seconds since 1970               */
    rtc_datetime_t utc;         /* current UTC                                   */
    rtc_datetime_t local;       /* current local time (utc + tz)                 */
    int16_t        tz_min;      /* time zone, minutes east of UTC                */
    int16_t        calib_ppm_x10; /* applied crystal correction, ppm * 10        */
    uint32_t       sync_age_s;  /* seconds since the last accepted sync           */
    uint32_t       minute_ticks;/* ALARM1 interrupts since boot                  */
} rtc_clock_state_t;

/*******************************************************************************
* API
*******************************************************************************/
/* Checks the WCO / backup clock, reads the RTC, creates the mutex and the
 * minute semaphore and enables ALARM1 (second 00 of every minute) on the
 * CM55 NVIC. Call once from a task, before the other functions. */
cy_rslt_t rtc_clock_init(void);

/* Snapshot of the clock (thread safe). */
void rtc_clock_get_state(rtc_clock_state_t *st);

/* Sets the clock from a source. Returns true if the RTC was written.
 * A source of lower quality than the last one is ignored unless the last
 * sync is older than RTC_SYNC_STALE_S; RTC_SRC_UART is a manual set and
 * is always accepted. */
bool rtc_clock_sync(rtc_src_t src, int64_t unix_utc);

/* Same, from a local date/time (uses the current time zone). */
bool rtc_clock_set_local(rtc_src_t src, const rtc_datetime_t *local);

/* Time zone in minutes east of UTC (RAM only; see README for persistence). */
void rtc_clock_set_tz(int16_t tz_min);

/* Crystal correction in ppm * 10 (+ = crystal slow, add pulses). Returns
 * false if the RTC refused the setting. */
bool rtc_clock_set_calibration(int16_t ppm_x10);

/* Blocks until the next ALARM1 (second 00) or the timeout. Returns true on
 * an alarm. Used by the low-power clock mode: the CPU sleeps in between. */
bool rtc_clock_wait_minute(uint32_t timeout_ms);

/* Releases a task blocked in rtc_clock_wait_minute() early (task context),
 * e.g. to leave the low-power clock mode at once. */
void rtc_clock_wake_waiter(void);

/* Name of a source for the HUD and the UART log: "NONE", "KEPT", ... */
const char *rtc_clock_src_name(rtc_src_t src);

/* Future hooks: call these from the GNSS / Bluetooth code. They map to
 * rtc_clock_sync() with the matching source. */
bool rtc_clock_sync_from_gnss(int64_t unix_utc);
bool rtc_clock_sync_from_phone(int64_t unix_utc, int16_t tz_min);

#ifdef __cplusplus
}
#endif

#endif /* RTC_CLOCK_H */
