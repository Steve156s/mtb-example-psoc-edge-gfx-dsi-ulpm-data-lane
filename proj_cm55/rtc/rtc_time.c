/*******************************************************************************
* File Name        : rtc_time.c
*
* Description      : Hardware independent date / time helpers. See rtc_time.h.
*
*******************************************************************************/
#include "rtc_time.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define SECONDS_PER_DAY     (86400LL)

static const char * const wday_names[8] =
{
    "---", "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};


bool rtc_time_is_leap(int year)
{
    return ((0 == (year % 4)) && (0 != (year % 100))) || (0 == (year % 400));
}


uint8_t rtc_time_days_in_month(int year, int month)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if ((month < 1) || (month > 12))
    {
        return 0U;
    }
    if ((2 == month) && rtc_time_is_leap(year))
    {
        return 29U;
    }
    return days[month - 1];
}


/* Days since 1970-01-01 for a civil date (H. Hinnant's algorithm). */
static int64_t days_from_civil(int y, int m, int d)
{
    int64_t era;
    int64_t yoe, doy, doe;

    y -= (m <= 2) ? 1 : 0;
    era = (y >= 0 ? y : (y - 399)) / 400;
    yoe = (int64_t)y - (era * 400);
    doy = ((153 * (int64_t)(m + ((m > 2) ? -3 : 9))) + 2) / 5 + (int64_t)d - 1;
    doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;
    return (era * 146097) + doe - 719468;
}


static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    int64_t era, doe, yoe, doy, mp;

    z += 719468;
    era = ((z >= 0) ? z : (z - 146096)) / 146097;
    doe = z - (era * 146097);
    yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
    doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
    mp  = ((5 * doy) + 2) / 153;
    *d  = (int)(doy - (((153 * mp) + 2) / 5) + 1);
    *m  = (int)((mp < 10) ? (mp + 3) : (mp - 9));
    *y  = (int)(yoe + (era * 400) + ((*m <= 2) ? 1 : 0));
}


uint8_t rtc_time_day_of_week(int year, int month, int day)
{
    /* 1970-01-01 was a Thursday (5 in 1 = Sunday numbering) */
    int64_t days = days_from_civil(year, month, day);
    int64_t w = (days + 4) % 7;     /* 0 = Sunday */

    if (w < 0)
    {
        w += 7;
    }
    return (uint8_t)(w + 1);
}


bool rtc_time_is_valid(const rtc_datetime_t *dt)
{
    if ((NULL == dt) ||
        (dt->year < RTC_TIME_YEAR_BASE) || (dt->year > RTC_TIME_YEAR_MAX) ||
        (dt->hour > 23U) || (dt->min > 59U) || (dt->sec > 59U))
    {
        return false;
    }
    return (dt->day >= 1U) && (dt->day <= rtc_time_days_in_month(dt->year, dt->month));
}


int64_t rtc_time_to_unix(const rtc_datetime_t *dt)
{
    return (days_from_civil(dt->year, dt->month, dt->day) * SECONDS_PER_DAY) +
           ((int64_t)dt->hour * 3600) + ((int64_t)dt->min * 60) + (int64_t)dt->sec;
}


void rtc_time_from_unix(int64_t unix_s, rtc_datetime_t *dt)
{
    int64_t days = unix_s / SECONDS_PER_DAY;
    int64_t rem  = unix_s % SECONDS_PER_DAY;
    int y, m, d;

    if (rem < 0)
    {
        rem += SECONDS_PER_DAY;
        days -= 1;
    }
    civil_from_days(days, &y, &m, &d);

    dt->year  = (int16_t)y;
    dt->month = (uint8_t)m;
    dt->day   = (uint8_t)d;
    dt->hour  = (uint8_t)(rem / 3600);
    dt->min   = (uint8_t)((rem % 3600) / 60);
    dt->sec   = (uint8_t)(rem % 60);
    dt->wday  = rtc_time_day_of_week(y, m, d);
}


void rtc_time_add_seconds(rtc_datetime_t *dt, int32_t offset_s)
{
    rtc_time_from_unix(rtc_time_to_unix(dt) + offset_s, dt);
}


uint8_t rtc_time_ppm_to_calib(int16_t ppm_x10, bool *add_pulses)
{
    /* one step = 128 / (32768 * 3600) = 1.0851 ppm = 10.851 ppm*10 */
    int32_t mag = (ppm_x10 < 0) ? -(int32_t)ppm_x10 : (int32_t)ppm_x10;
    int32_t step = ((mag * 1000) + 5425) / 10851;   /* rounded */

    if (NULL != add_pulses)
    {
        *add_pulses = (ppm_x10 > 0);
    }
    if (step > 63)
    {
        step = 63;
    }
    return (uint8_t)step;
}


/* ---------------------------------------------------------------------------
 * Command parser
 * ------------------------------------------------------------------------- */

static const char *skip_space(const char *p)
{
    while ((' ' == *p) || ('\t' == *p))
    {
        p++;
    }
    return p;
}


/* Reads an unsigned decimal number of at most max_digits digits. */
static bool read_uint(const char **pp, int max_digits, int64_t *out)
{
    const char *p = *pp;
    int64_t v = 0;
    int n = 0;

    while (isdigit((unsigned char)*p) && (n < max_digits))
    {
        v = (v * 10) + (*p - '0');
        p++;
        n++;
    }
    if (0 == n)
    {
        return false;
    }
    *pp = p;
    *out = v;
    return true;
}


static bool expect_char(const char **pp, char c)
{
    if (**pp == c)
    {
        (*pp)++;
        return true;
    }
    return false;
}


/* "YYYY-MM-DD HH:MM[:SS]" (also accepts 'T' or '/' separators) */
static bool parse_datetime(const char *p, rtc_datetime_t *dt)
{
    int64_t y, mo, d, h, mi, s = 0;

    p = skip_space(p);
    if (!read_uint(&p, 4, &y) || !(expect_char(&p, '-') || expect_char(&p, '/')) ||
        !read_uint(&p, 2, &mo) || !(expect_char(&p, '-') || expect_char(&p, '/')) ||
        !read_uint(&p, 2, &d))
    {
        return false;
    }
    if (!(expect_char(&p, 'T') || expect_char(&p, 't')))
    {
        if ((' ' != *p) && ('\t' != *p))
        {
            return false;
        }
        p = skip_space(p);
    }
    if (!read_uint(&p, 2, &h) || !expect_char(&p, ':') || !read_uint(&p, 2, &mi))
    {
        return false;
    }
    if (expect_char(&p, ':') && !read_uint(&p, 2, &s))
    {
        return false;
    }
    if ('\0' != *skip_space(p))
    {
        return false;
    }

    dt->year  = (int16_t)y;
    dt->month = (uint8_t)mo;
    dt->day   = (uint8_t)d;
    dt->hour  = (uint8_t)h;
    dt->min   = (uint8_t)mi;
    dt->sec   = (uint8_t)s;
    if (!rtc_time_is_valid(dt))
    {
        return false;
    }
    dt->wday = rtc_time_day_of_week(dt->year, dt->month, dt->day);
    return true;
}


/* "+8", "-3:30", "+0545", "+05:45", "0" */
static bool parse_tz(const char *p, int16_t *tz_min)
{
    int sign = 1;
    int64_t h = 0, m = 0;
    const char *start;
    int32_t total;

    p = skip_space(p);
    if ('-' == *p)
    {
        sign = -1;
        p++;
    }
    else if ('+' == *p)
    {
        p++;
    }
    start = p;
    if (!read_uint(&p, 4, &h))
    {
        return false;
    }
    if ((p - start) > 2)        /* "0545" -> 5 h 45 min */
    {
        m = h % 100;
        h = h / 100;
    }
    else if (expect_char(&p, ':') && !read_uint(&p, 2, &m))
    {
        return false;
    }
    if (('\0' != *skip_space(p)) || (m > 59))
    {
        return false;
    }
    total = sign * (int32_t)((h * 60) + m);
    if ((total < RTC_TIME_TZ_MIN_MINUTES) || (total > RTC_TIME_TZ_MAX_MINUTES))
    {
        return false;
    }
    *tz_min = (int16_t)total;
    return true;
}


/* "+2.5", "-12", "0.3" -> ppm * 10, range -69.0 .. +69.0 */
static bool parse_ppm(const char *p, int16_t *ppm_x10)
{
    int sign = 1;
    int64_t ip = 0, fp = 0;

    p = skip_space(p);
    if ('-' == *p)
    {
        sign = -1;
        p++;
    }
    else if ('+' == *p)
    {
        p++;
    }
    if (!read_uint(&p, 3, &ip))
    {
        return false;
    }
    if (expect_char(&p, '.') && !read_uint(&p, 1, &fp))
    {
        return false;
    }
    if ('\0' != *skip_space(p))
    {
        return false;
    }
    ip = (ip * 10) + fp;
    if (ip > 690)
    {
        return false;
    }
    *ppm_x10 = (int16_t)(sign * ip);
    return true;
}


rtc_cmd_type_t rtc_time_parse_command(const char *line, rtc_cmd_t *cmd)
{
    const char *p;
    char c;
    int64_t v;

    memset(cmd, 0, sizeof(*cmd));
    cmd->type = RTC_CMD_ERROR;

    if (NULL == line)
    {
        cmd->type = RTC_CMD_NONE;
        return cmd->type;
    }
    p = skip_space(line);
    if ('\0' == *p)
    {
        cmd->type = RTC_CMD_NONE;
        return cmd->type;
    }

    c = (char)toupper((unsigned char)*p);
    p++;
    /* The command letter must be followed by a blank or the end of line */
    if (('\0' != *p) && (' ' != *p) && ('\t' != *p))
    {
        return cmd->type;
    }

    switch (c)
    {
        case 'T':
            if (parse_datetime(p, &cmd->local))
            {
                cmd->type = RTC_CMD_SET_LOCAL;
            }
            break;

        case 'U':
            p = skip_space(p);
            if (read_uint(&p, 12, &v) && ('\0' == *skip_space(p)) &&
                (v >= RTC_TIME_UNIX_2000) && (v < 4102444800LL /* 2100-01-01 */))
            {
                cmd->unix_utc = v;
                cmd->type = RTC_CMD_SET_UTC;
            }
            break;

        case 'Z':
            if (parse_tz(p, &cmd->tz_min))
            {
                cmd->type = RTC_CMD_SET_TZ;
            }
            break;

        case 'C':
            if (parse_ppm(p, &cmd->ppm_x10))
            {
                cmd->type = RTC_CMD_SET_CALIB;
            }
            break;

        case 'L':
            p = skip_space(p);
            if ((('0' == p[0]) || ('1' == p[0])) && ('\0' == *skip_space(p + 1)))
            {
                cmd->enable = ('1' == p[0]);
                cmd->type = RTC_CMD_LOW_POWER;
            }
            break;

        case 'S':
        case '?':
            if ('\0' == *skip_space(p))
            {
                cmd->type = RTC_CMD_STATUS;
            }
            break;

        case 'H':
            if ('\0' == *skip_space(p))
            {
                cmd->type = RTC_CMD_HELP;
            }
            break;

        default:
            break;
    }
    return cmd->type;
}


/* ---------------------------------------------------------------------------
 * Formatting
 * ------------------------------------------------------------------------- */

char *rtc_time_format_date(const rtc_datetime_t *dt, char *buf, size_t len)
{
    (void)snprintf(buf, len, "%04d-%02u-%02u", (int)dt->year,
                   (unsigned)dt->month, (unsigned)dt->day);
    return buf;
}


char *rtc_time_format_time(const rtc_datetime_t *dt, char *buf, size_t len)
{
    (void)snprintf(buf, len, "%02u:%02u:%02u", (unsigned)dt->hour,
                   (unsigned)dt->min, (unsigned)dt->sec);
    return buf;
}


const char *rtc_time_wday_name(uint8_t wday)
{
    return wday_names[(wday <= 7U) ? wday : 0U];
}


char *rtc_time_format_tz(int16_t tz_min, char *buf, size_t len)
{
    int32_t a = (tz_min < 0) ? -(int32_t)tz_min : (int32_t)tz_min;

    (void)snprintf(buf, len, "UTC%c%02d:%02d", (tz_min < 0) ? '-' : '+',
                   (int)(a / 60), (int)(a % 60));
    return buf;
}
