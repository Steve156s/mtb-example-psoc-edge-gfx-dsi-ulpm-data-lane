/*
 * Host unit test for the hardware independent SHT40 helpers
 * (proj_cm55/humidity/sht40_math.c, sht40_hud.c).
 *
 *   make -C tools/sht40_host_test run
 *
 * Also writes hud_preview.ppm (panel rendering for the README / review).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sht40_math.h"
#include "sht40_hud.h"

static int failures = 0;

#define CHECK_NEAR(name, got, want, tol)                                       \
    do {                                                                       \
        float g_ = (got), w_ = (want);                                         \
        if (fabsf(g_ - w_) > (tol)) {                                          \
            printf("FAIL %-36s got %8.3f want %8.3f\n", (name), g_, w_);       \
            failures++;                                                        \
        } else {                                                               \
            printf("ok   %-36s %8.3f\n", (name), g_);                         \
        }                                                                      \
    } while (0)

#define CHECK(name, cond)                                                      \
    do {                                                                       \
        if (!(cond)) { printf("FAIL %s\n", (name)); failures++; }              \
        else         { printf("ok   %s\n", (name)); }                          \
    } while (0)

static void test_dew_point(void)
{
    /* Reference values: Magnus 17.62 / 243.12 (Sensirion app note) */
    CHECK_NEAR("dew 20C 50%",   sht40_dew_point_c(20.0f, 50.0f),  9.26f, 0.05f);
    CHECK_NEAR("dew 25C 60%",   sht40_dew_point_c(25.0f, 60.0f), 16.69f, 0.05f);
    CHECK_NEAR("dew 0C 80%",    sht40_dew_point_c(0.0f, 80.0f),  -3.01f, 0.05f);
    CHECK_NEAR("dew 10C 100%",  sht40_dew_point_c(10.0f, 100.0f), 10.0f, 0.01f);
    CHECK_NEAR("dew -10C 90%",  sht40_dew_point_c(-10.0f, 90.0f), -11.33f, 0.05f);
    CHECK("dew RH 0 finite", isfinite(sht40_dew_point_c(20.0f, 0.0f)));
    CHECK("dew RH>100 clamped", fabsf(sht40_dew_point_c(5.0f, 103.0f) - 5.0f) < 0.01f);
}

static void test_frost_point(void)
{
    CHECK_NEAR("frost -10C 90%", sht40_frost_point_c(-10.0f, 90.0f), -10.06f, 0.05f);
    CHECK("frost > dew below 0C",
          sht40_frost_point_c(-5.0f, 70.0f) > sht40_dew_point_c(-5.0f, 70.0f));
    CHECK("frost = dew above 0C",
          sht40_frost_point_c(5.0f, 70.0f) == sht40_dew_point_c(5.0f, 70.0f));
}

static void test_abs_humidity(void)
{
    CHECK_NEAR("AH 20C 50%",  sht40_abs_humidity_gm3(20.0f, 50.0f), 8.65f, 0.1f);
    CHECK_NEAR("AH 0C 100%",  sht40_abs_humidity_gm3(0.0f, 100.0f), 4.85f, 0.1f);
}

static void test_fog(void)
{
    const float risk = 2.0f, fog = 0.0f, hyst = 0.5f;
    sht40_fog_level_t l = SHT40_FOG_OK;

    l = sht40_fog_update(l, 5.0f, risk, fog, hyst);  CHECK("fog 5.0 OK",       l == SHT40_FOG_OK);
    l = sht40_fog_update(l, 1.5f, risk, fog, hyst);  CHECK("fog 1.5 RISK",     l == SHT40_FOG_RISK);
    l = sht40_fog_update(l, 2.3f, risk, fog, hyst);  CHECK("fog 2.3 hold RISK",l == SHT40_FOG_RISK);
    l = sht40_fog_update(l, -0.2f, risk, fog, hyst); CHECK("fog -0.2 FOG",     l == SHT40_FOG_LIKELY);
    l = sht40_fog_update(l, 0.4f, risk, fog, hyst);  CHECK("fog 0.4 hold FOG", l == SHT40_FOG_LIKELY);
    l = sht40_fog_update(l, 1.0f, risk, fog, hyst);  CHECK("fog 1.0 RISK",     l == SHT40_FOG_RISK);
    l = sht40_fog_update(l, 2.6f, risk, fog, hyst);  CHECK("fog 2.6 OK",       l == SHT40_FOG_OK);
    l = sht40_fog_update(SHT40_FOG_LIKELY, 4.0f, risk, fog, hyst);
    CHECK("fog FOG -> 4.0 OK", l == SHT40_FOG_OK);
}

static void test_heater(void)
{
    bool on = false;
    on = sht40_batt_heater_update(on, 3.0f, 0.0f, 5.0f);   CHECK("heat 3C stays OFF", !on);
    on = sht40_batt_heater_update(on, -0.5f, 0.0f, 5.0f);  CHECK("heat -0.5C ON", on);
    on = sht40_batt_heater_update(on, 3.0f, 0.0f, 5.0f);   CHECK("heat 3C stays ON", on);
    on = sht40_batt_heater_update(on, 5.0f, 0.0f, 5.0f);   CHECK("heat 5C OFF", !on);
}

static void test_format(void)
{
    char b[16];
    sht40_format_fixed(b, sizeof(b), -5.24f, 1U, false); CHECK("fmt -5.2",  0 == strcmp(b, "-5.2"));
    sht40_format_fixed(b, sizeof(b), 78.45f, 1U, false); CHECK("fmt 78.5",  0 == strcmp(b, "78.5"));
    sht40_format_fixed(b, sizeof(b), -0.04f, 1U, false); CHECK("fmt 0.0",   0 == strcmp(b, "0.0"));
    sht40_format_fixed(b, sizeof(b), 3.0f, 0U, true);    CHECK("fmt +3",    0 == strcmp(b, "+3"));
    sht40_format_fixed(b, 4U, 123.4f, 1U, false);        CHECK("fmt trunc", 3U == strlen(b));
}

static void write_preview(void)
{
    enum { PW = SHT40_HUD_WIDTH, PH = SHT40_HUD_HEIGHT, GAP = 8 };
    static uint16_t fb[(PH * 3 + GAP * 2) * PW];
    sht40_hud_view_t v;
    FILE* f;

    memset(fb, 0x40, sizeof(fb));

    v.mode = SHT40_HUD_MODE_DATA;
    v.temperature_c = 12.4f; v.humidity_pct = 48.2f;
    v.dew_point_c = sht40_dew_point_c(12.4f, 48.2f);
    v.fog_level = SHT40_FOG_OK; v.batt_heater_on = false;
    sht40_hud_draw(fb, PW, PH * 3 + GAP * 2, 0, 0, &v);

    v.temperature_c = -4.8f; v.humidity_pct = 86.0f;
    v.dew_point_c = sht40_dew_point_c(-4.8f, 86.0f);
    v.fog_level = SHT40_FOG_RISK; v.batt_heater_on = true;
    sht40_hud_draw(fb, PW, PH * 3 + GAP * 2, 0, PH + GAP, &v);

    v.mode = SHT40_HUD_MODE_NO_SENSOR;
    sht40_hud_draw(fb, PW, PH * 3 + GAP * 2, 0, 2 * (PH + GAP), &v);

    f = fopen("hud_preview.ppm", "wb");
    if (NULL == f) { return; }
    fprintf(f, "P6\n%d %d\n255\n", PW, PH * 3 + GAP * 2);
    for (size_t i = 0; i < sizeof(fb) / sizeof(fb[0]); i++)
    {
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(((fb[i] >> 11) & 0x1F) << 3);
        rgb[1] = (unsigned char)(((fb[i] >> 5) & 0x3F) << 2);
        rgb[2] = (unsigned char)((fb[i] & 0x1F) << 3);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote hud_preview.ppm (%dx%d)\n", PW, PH * 3 + GAP * 2);
}

int main(void)
{
    test_dew_point();
    test_frost_point();
    test_abs_humidity();
    test_fog();
    test_heater();
    test_format();
    write_preview();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, (failures == 1) ? "" : "s");
    return failures ? 1 : 0;
}
