/* Host unit test for proj_cm55/rtc/rtc_time.c and rtc_hud.c.
 * Build and run: make -C tools/rtc_host_test run */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rtc_time.h"
#include "rtc_hud.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_calendar(void)
{
    rtc_datetime_t dt = { 2026, 11, 22, 10, 5, 9, 0 };
    rtc_datetime_t back;

    CHECK(rtc_time_is_leap(2024));
    CHECK(!rtc_time_is_leap(2100));
    CHECK(rtc_time_is_leap(2000));
    CHECK(29 == rtc_time_days_in_month(2028, 2));
    CHECK(28 == rtc_time_days_in_month(2026, 2));
    CHECK(1 == rtc_time_day_of_week(2026, 11, 22));   /* Sunday */
    CHECK(7 == rtc_time_day_of_week(2000, 1, 1));     /* Saturday */

    CHECK(1795341909LL == rtc_time_to_unix(&dt));
    rtc_time_from_unix(1795341909LL, &back);
    CHECK((2026 == back.year) && (11 == back.month) && (22 == back.day));
    CHECK((10 == back.hour) && (5 == back.min) && (9 == back.sec) && (1 == back.wday));

    /* compare against the C library over 2000..2099, every ~10 days */
    for (long long t = RTC_TIME_UNIX_2000; t < 4102444800LL; t += 863987)
    {
        time_t tt = (time_t)t;
        struct tm g;
        gmtime_r(&tt, &g);
        rtc_time_from_unix(t, &back);
        CHECK(back.year == g.tm_year + 1900);
        CHECK(back.month == g.tm_mon + 1);
        CHECK(back.day == g.tm_mday);
        CHECK(back.hour == g.tm_hour && back.min == g.tm_min && back.sec == g.tm_sec);
        CHECK(back.wday == g.tm_wday + 1);
        CHECK(rtc_time_to_unix(&back) == t);
    }

    /* time zone crossing midnight / year end */
    dt = (rtc_datetime_t){ 2026, 12, 31, 20, 30, 0, 0 };
    rtc_time_add_seconds(&dt, 8 * 3600);
    CHECK((2027 == dt.year) && (1 == dt.month) && (1 == dt.day) && (4 == dt.hour));
    rtc_time_add_seconds(&dt, -(8 * 3600));
    CHECK((2026 == dt.year) && (31 == dt.day) && (20 == dt.hour));

    dt = (rtc_datetime_t){ 2026, 2, 29, 0, 0, 0, 0 };
    CHECK(!rtc_time_is_valid(&dt));
    dt.year = 2028;
    CHECK(rtc_time_is_valid(&dt));
    dt.year = 1999;
    CHECK(!rtc_time_is_valid(&dt));
}

static void test_calib(void)
{
    bool add = false;
    CHECK(0 == rtc_time_ppm_to_calib(0, &add) && !add);
    CHECK(2 == rtc_time_ppm_to_calib(25, &add) && add);     /* +2.5 ppm */
    CHECK(9 == rtc_time_ppm_to_calib(-100, &add) && !add);  /* -10 ppm  */
    CHECK(63 == rtc_time_ppm_to_calib(690, &add));
}

static void test_parser(void)
{
    rtc_cmd_t c;

    CHECK(RTC_CMD_SET_LOCAL == rtc_time_parse_command("T 2026-11-22 10:05:09", &c));
    CHECK(c.local.year == 2026 && c.local.month == 11 && c.local.day == 22);
    CHECK(c.local.hour == 10 && c.local.min == 5 && c.local.sec == 9 && c.local.wday == 1);
    CHECK(RTC_CMD_SET_LOCAL == rtc_time_parse_command("t 2026/1/2 7:08", &c));
    CHECK(c.local.month == 1 && c.local.day == 2 && c.local.hour == 7 && c.local.sec == 0);
    CHECK(RTC_CMD_SET_LOCAL == rtc_time_parse_command("T 2026-11-22T23:59:59", &c));
    CHECK(RTC_CMD_ERROR == rtc_time_parse_command("T 2026-02-30 10:00", &c));
    CHECK(RTC_CMD_ERROR == rtc_time_parse_command("T 2026-11-22 24:00", &c));
    CHECK(RTC_CMD_ERROR == rtc_time_parse_command("T 2026-11-22 10:00 x", &c));
    CHECK(RTC_CMD_ERROR == rtc_time_parse_command("T 1999-11-22 10:00", &c));

    CHECK(RTC_CMD_SET_UTC == rtc_time_parse_command("U 1795341909", &c));
    CHECK(1795341909LL == c.unix_utc);
    CHECK(RTC_CMD_ERROR == rtc_time_parse_command("U 12345", &c));

    CHECK(RTC_CMD_SET_TZ == rtc_time_parse_command("Z +8", &c) && 480 == c.tz_min);
    CHECK(RTC_CMD_SET_TZ == rtc_time_parse_command("z -3:30", &c) && -210 == c.tz_min);
    CHECK(RTC_CMD_SET_TZ == rtc_time_parse_command("Z +0545", &c) && 345 == c.tz_min);
    CHECK(RTC_CMD_SET_TZ == rtc_time_parse_command("Z 0", &c) && 0 == c.tz_min);
    CHECK(RTC_CMD_ERROR == rtc_time_parse_command("Z +15", &c));

    CHECK(RTC_CMD_SET_CALIB == rtc_time_parse_command("C +2.5", &c) && 25 == c.ppm_x10);
    CHECK(RTC_CMD_SET_CALIB == rtc_time_parse_command("C -12", &c) && -120 == c.ppm_x10);
    CHECK(RTC_CMD_LOW_POWER == rtc_time_parse_command("L 1", &c) && c.enable);
    CHECK(RTC_CMD_LOW_POWER == rtc_time_parse_command("l 0", &c) && !c.enable);
    CHECK(RTC_CMD_STATUS == rtc_time_parse_command("?", &c));
    CHECK(RTC_CMD_STATUS == rtc_time_parse_command(" s ", &c));
    CHECK(RTC_CMD_HELP == rtc_time_parse_command("H", &c));
    CHECK(RTC_CMD_NONE == rtc_time_parse_command("   ", &c));
    CHECK(RTC_CMD_ERROR == rtc_time_parse_command("TIME", &c));
    CHECK(RTC_CMD_ERROR == rtc_time_parse_command("X 1", &c));
}

static void test_format(void)
{
    char b[16];
    rtc_datetime_t dt = { 2026, 1, 2, 3, 4, 5, 6 };
    CHECK(0 == strcmp("2026-01-02", rtc_time_format_date(&dt, b, sizeof(b))));
    CHECK(0 == strcmp("03:04:05", rtc_time_format_time(&dt, b, sizeof(b))));
    CHECK(0 == strcmp("FRI", rtc_time_wday_name(6)));
    CHECK(0 == strcmp("UTC+08:00", rtc_time_format_tz(480, b, sizeof(b))));
    CHECK(0 == strcmp("UTC-03:30", rtc_time_format_tz(-210, b, sizeof(b))));
    CHECK(0 == strcmp("45S", rtc_hud_format_age(45, b, sizeof(b))));
    CHECK(0 == strcmp("3M", rtc_hud_format_age(200, b, sizeof(b))));
    CHECK(0 == strcmp("2D", rtc_hud_format_age(200000, b, sizeof(b))));
}

/* Renders the panel into a small RGB565 buffer and writes a PPM preview. */
static void render(const char *path, const rtc_hud_view_t *v)
{
    const uint32_t w = RTC_HUD_WIDTH + 16U, h = RTC_HUD_HEIGHT + 16U;
    uint16_t *fb = calloc((size_t)w * h, sizeof(uint16_t));
    FILE *f;

    for (uint32_t i = 0; i < w * h; i++) { fb[i] = RTC_HUD_RGB565(20, 20, 30); }
    rtc_hud_draw(fb, w, h, 8U, 8U, v);
    /* nothing may be drawn outside the panel */
    CHECK(fb[0] == RTC_HUD_RGB565(20, 20, 30));
    CHECK(fb[(h - 1U) * w + (w - 1U)] == RTC_HUD_RGB565(20, 20, 30));

    f = fopen(path, "wb");
    if (NULL != f)
    {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (uint32_t i = 0; i < w * h; i++)
        {
            unsigned char px[3] = { (unsigned char)((fb[i] >> 8) & 0xF8),
                                    (unsigned char)((fb[i] >> 3) & 0xFC),
                                    (unsigned char)((fb[i] << 3) & 0xF8) };
            fwrite(px, 1, 3, f);
        }
        fclose(f);
    }
    free(fb);
}

int main(void)
{
    rtc_hud_view_t v = { { 2026, 11, 22, 10, 5, 9, 1 }, 480, true, true, false, "GNSS", 180 };

    test_calendar();
    test_calib();
    test_parser();
    test_format();

    render("hud_preview.ppm", &v);
    v.low_power = true;
    render("hud_preview_lp.ppm", &v);
    v.low_power = false;
    v.time_valid = false;
    render("hud_preview_unset.ppm", &v);

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
