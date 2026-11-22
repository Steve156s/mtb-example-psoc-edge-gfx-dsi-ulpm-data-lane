/*******************************************************************************
* File Name        : rtc_time.h
*
* Description      : Hardware independent date / time helpers for the HUD
*                    goggle clock: civil date <-> Unix time (UTC), time zone
*                    offset, day of week, RTC calibration step conversion and
*                    the UART time-set command parser.
*
*                    Nothing in this file touches the PSOC Edge hardware, so it
*                    is also built on the host by tools/rtc_host_test.
*
* Related Document : See README.md, section "RTC clock (WCO 32.768 kHz)"
*
*******************************************************************************/
#ifndef RTC_TIME_H
#define RTC_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The PSOC Edge RTC keeps a two digit year (00..99 = 2000..2099). */
#define RTC_TIME_YEAR_BASE          (2000)
#define RTC_TIME_YEAR_MAX           (2099)

/* Unix time of 2000-01-01 00:00:00 UTC (first second the RTC can hold). */
#define RTC_TIME_UNIX_2000          (946684800LL)

/* Allowed time zone offsets: UTC-12:00 .. UTC+14:00 in minutes. */
#define RTC_TIME_TZ_MIN_MINUTES     (-12 * 60)
#define RTC_TIME_TZ_MAX_MINUTES     (14 * 60)

/* Calendar date and time. year is the full year (2026), month 1..12,
 * day 1..31, wday 1..7 with 1 = Sunday (same numbering as CY_RTC_SUNDAY..). */
typedef struct
{
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint8_t wday;
} rtc_datetime_t;

/* Result of parsing one UART command line (see rtc_time_parse_command). */
typedef enum
{
    RTC_CMD_NONE = 0,       /* empty line                                    */
    RTC_CMD_SET_LOCAL,      /* "T YYYY-MM-DD HH:MM[:SS]"  local time          */
    RTC_CMD_SET_UTC,        /* "U <unix seconds>"         UTC from GNSS/phone */
    RTC_CMD_SET_TZ,         /* "Z +8" / "Z -3:30" / "Z +0545"                 */
    RTC_CMD_SET_CALIB,      /* "C +2.5"  crystal error in ppm (+ = too slow)  */
    RTC_CMD_LOW_POWER,      /* "L 1" / "L 0"  low-power clock-only mode       */
    RTC_CMD_STATUS,         /* "S" or "?"                                    */
    RTC_CMD_HELP,           /* "H"                                           */
    RTC_CMD_ERROR           /* unknown command or value out of range         */
} rtc_cmd_type_t;

typedef struct
{
    rtc_cmd_type_t type;
    rtc_datetime_t local;   /* RTC_CMD_SET_LOCAL                      */
    int64_t        unix_utc;/* RTC_CMD_SET_UTC                        */
    int16_t        tz_min;  /* RTC_CMD_SET_TZ, minutes east of UTC    */
    int16_t        ppm_x10; /* RTC_CMD_SET_CALIB, ppm * 10            */
    bool           enable;  /* RTC_CMD_LOW_POWER                      */
} rtc_cmd_t;

/* true for leap years in the Gregorian calendar. */
bool rtc_time_is_leap(int year);

/* Number of days in the month (1..12) of the year, 0 for an invalid month. */
uint8_t rtc_time_days_in_month(int year, int month);

/* Day of week 1..7 (1 = Sunday) for a valid date. */
uint8_t rtc_time_day_of_week(int year, int month, int day);

/* true if every field is in range and the year fits the RTC (2000..2099).
 * wday is not checked. */
bool rtc_time_is_valid(const rtc_datetime_t *dt);

/* Calendar date/time -> seconds since 1970-01-01 00:00:00 (no leap seconds).
 * The date is not validated. */
int64_t rtc_time_to_unix(const rtc_datetime_t *dt);

/* Seconds since 1970 -> calendar date/time, wday filled in. */
void rtc_time_from_unix(int64_t unix_s, rtc_datetime_t *dt);

/* Adds offset_s seconds (may be negative) and normalises the result. */
void rtc_time_add_seconds(rtc_datetime_t *dt, int32_t offset_s);

/* Converts a crystal frequency error in ppm*10 to the RTC calibration step
 * (each step adds or removes 128 WCO cycles per hour = 1.085 ppm).
 * Positive ppm means the crystal is slow and pulses must be added.
 * Returns the clamped step 0..63 and writes the sign (true = add pulses). */
uint8_t rtc_time_ppm_to_calib(int16_t ppm_x10, bool *add_pulses);

/* Parses one command line (case insensitive, CR/LF already stripped).
 * Returns the command type; values are written to *cmd. */
rtc_cmd_type_t rtc_time_parse_command(const char *line, rtc_cmd_t *cmd);

/* "2026-11-22" / "10:05:09" / "SUN" / "UTC+08:00" helpers. buf must hold at
 * least 11, 9, 4 and 10 characters. Return buf. */
char *rtc_time_format_date(const rtc_datetime_t *dt, char *buf, size_t len);
char *rtc_time_format_time(const rtc_datetime_t *dt, char *buf, size_t len);
const char *rtc_time_wday_name(uint8_t wday);
char *rtc_time_format_tz(int16_t tz_min, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RTC_TIME_H */
