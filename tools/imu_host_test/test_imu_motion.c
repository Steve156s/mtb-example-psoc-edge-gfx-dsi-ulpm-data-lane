/*
 * Host-side unit test for proj_cm55/imu/imu_motion.c and hud_text.c.
 * Build & run:  make        (needs gcc, no ModusToolbox)
 * Also writes hud_preview.ppm so the font / layout can be eyeballed.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imu_motion.h"
#include "hud_text.h"

#define DT_MS   20U
#define DEG2RAD 0.01745329252f

static int failures = 0;
static uint32_t t_ms = 1000U;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: "); \
    printf(__VA_ARGS__); printf("\n"); } else { printf("  ok:   "); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Gravity in body frame for a given roll / pitch (same convention as filter) */
static void gravity(float roll_deg, float pitch_deg, float a[3])
{
    float r = roll_deg * DEG2RAD, p = pitch_deg * DEG2RAD;
    a[0] = -sinf(p);
    a[1] = cosf(p) * sinf(r);
    a[2] = cosf(p) * cosf(r);
}

static uint32_t step(imu_motion_t *m, const float a[3], const float g[3])
{
    t_ms += DT_MS;
    return imu_motion_update(m, a, g, DT_MS / 1000.0f, t_ms);
}

static uint32_t hold(imu_motion_t *m, float roll, float pitch, uint32_t ms)
{
    float a[3], g[3] = { 0, 0, 0 };
    uint32_t ev = 0;
    gravity(roll, pitch, a);
    for (uint32_t i = 0; i < ms / DT_MS; i++) { ev |= step(m, a, g); }
    return ev;
}

static uint32_t spike(imu_motion_t *m, float g_level, uint32_t ms)
{
    float a[3] = { 0, 0, g_level }, g[3] = { 0, 0, 0 };
    uint32_t ev = 0;
    for (uint32_t i = 0; i < ms / DT_MS; i++) { ev |= step(m, a, g); }
    return ev;
}

static void test_static_tilt(void)
{
    imu_motion_t m;
    printf("static tilt\n");
    imu_motion_init(&m, NULL);
    hold(&m, 0, 0, 1000);
    CHECK(m.attitude_valid, "attitude valid after level hold");
    CHECK(fabsf(m.roll_deg) < 0.5f && fabsf(m.pitch_deg) < 0.5f,
          "level -> roll %.2f pitch %.2f", m.roll_deg, m.pitch_deg);

    hold(&m, 30, -20, 5000);
    CHECK(fabsf(m.roll_deg - 30) < 1.0f && fabsf(m.pitch_deg + 20) < 1.0f,
          "tilt 30/-20 -> roll %.2f pitch %.2f", m.roll_deg, m.pitch_deg);

    hold(&m, 175, 0, 3000);
    hold(&m, -175, 0, 5000);
    CHECK(fabsf(m.roll_deg + 175) < 1.5f,
          "roll across +/-180 wrap -> %.2f", m.roll_deg);
}

static void test_gyro_tracking(void)
{
    imu_motion_t m;
    float a[3], g[3] = { 90.0f, 0, 0 };
    printf("gyro tracking (roll 90 dps for 0.5 s, accel blocked by 1.5 g)\n");
    imu_motion_init(&m, NULL);
    hold(&m, 0, 0, 500);
    for (int i = 0; i < 25; i++)
    {
        gravity(i * 1.8f, 0, a);
        a[0] *= 1.5f; a[1] *= 1.5f; a[2] *= 1.5f;  /* linear accel: gyro only */
        step(&m, a, g);
    }
    CHECK(fabsf(m.roll_deg - 45.0f) < 2.0f, "integrated roll %.2f (expect 45)",
          m.roll_deg);
}

static void test_impact_no_fall(void)
{
    imu_motion_t m;
    uint32_t ev;
    printf("bump while upright\n");
    imu_motion_init(&m, NULL);
    hold(&m, 0, 0, 2000);
    ev = spike(&m, 6.0f, 60);
    CHECK(ev & IMU_EVENT_IMPACT, "IMPACT reported, peak %.1f g",
          imu_motion_last_impact_g(&m));
    ev = hold(&m, 0, 0, 5000);
    CHECK(!(ev & IMU_EVENT_FALL), "no FALL when still upright afterwards");
}

static void test_jump_landing(void)
{
    imu_motion_t m;
    uint32_t ev;
    printf("jump: 400 ms free fall, 5 g landing, stop upright\n");
    imu_motion_init(&m, NULL);
    hold(&m, 0, 5, 2000);
    ev = spike(&m, 0.05f, 400);
    CHECK(ev & IMU_EVENT_FREEFALL, "FREEFALL reported");
    ev = spike(&m, 5.0f, 40);
    CHECK(ev & IMU_EVENT_IMPACT, "IMPACT reported");
    ev = hold(&m, 0, 5, 5000);
    CHECK(!(ev & IMU_EVENT_FALL), "no FALL after upright landing");
}

static void test_fall(void)
{
    imu_motion_t m;
    uint32_t ev = 0;
    float a[3], g[3] = { 300.0f, 0, 0 };
    printf("fall: tumble to the side, 8 g impact, lie still on side\n");
    imu_motion_init(&m, NULL);
    hold(&m, 0, 0, 3000);
    /* tumble: 300 dps roll for 0.3 s with chaotic accel (gyro only) */
    for (int i = 0; i < 15; i++)
    {
        a[0] = 0.3f; a[1] = 1.8f; a[2] = 0.2f;
        ev |= step(&m, a, g);
    }
    ev |= spike(&m, 8.0f, 40);
    CHECK(ev & IMU_EVENT_IMPACT, "IMPACT reported");
    ev = hold(&m, 90, 0, 3000);
    CHECK(ev & IMU_EVENT_FALL, "FALL reported (roll now %.1f, ref %.1f)",
          m.roll_deg, m.ref_roll_deg);
}

static void test_get_up(void)
{
    imu_motion_t m;
    uint32_t ev;
    float a[3], g[3] = { 80.0f, 40.0f, 0 };
    printf("impact then keep moving (no still period)\n");
    imu_motion_init(&m, NULL);
    hold(&m, 0, 0, 2000);
    spike(&m, 6.0f, 40);
    ev = 0;
    for (int i = 0; i < 250; i++)
    {
        gravity(0, 0, a);
        g[0] = (i & 1) ? 80.0f : -80.0f;
        ev |= step(&m, a, g);
    }
    CHECK(!(ev & IMU_EVENT_FALL), "no FALL while moving");
    CHECK(!m.fall_watch, "fall watch closed after window");
}

static void test_format(void)
{
    char b[16];
    printf("fixed-point formatting\n");
    hud_format_fixed1(b, sizeof b, -12.34f, 6); CHECK(!strcmp(b, " -12.3"), "'%s'", b);
    hud_format_fixed1(b, sizeof b, 0.05f, 0);   CHECK(!strcmp(b, "0.1"), "'%s'", b);
    hud_format_fixed1(b, sizeof b, -0.04f, 0);  CHECK(!strcmp(b, "0.0"), "'%s'", b);
    hud_format_fixed1(b, sizeof b, 179.96f, 6); CHECK(!strcmp(b, " 180.0"), "'%s'", b);
}

static void write_preview(void)
{
    enum { W = 832, H = 480, VIS_W = 800 };
    static uint16_t fb[W * H];
    hud_surface_t s = { fb, W, VIS_W, H };
    for (int i = 0; i < W * H; i++) fb[i] = HUD_COLOR_WHITE;
    hud_fill_rect(&s, 16, 16, 280, 156, HUD_COLOR_PANEL);
    hud_draw_text(&s, 28, 28,  "BMI270        ", 3, HUD_COLOR_GREY,   HUD_COLOR_PANEL);
    hud_draw_text(&s, 28, 54,  "PITCH  -12.3  ", 3, HUD_COLOR_WHITE,  HUD_COLOR_PANEL);
    hud_draw_text(&s, 28, 80,  "ROLL    45.6  ", 3, HUD_COLOR_WHITE,  HUD_COLOR_PANEL);
    hud_draw_text(&s, 28, 106, "ACC G    1.0  ", 3, HUD_COLOR_WHITE,  HUD_COLOR_PANEL);
    hud_draw_text(&s, 28, 132, "IMPACT6.2G    ", 3, HUD_COLOR_YELLOW, HUD_COLOR_PANEL);
    hud_draw_text(&s, 16, 200, "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789 -.:+/%=!()|", 2,
                  HUD_COLOR_BLACK, HUD_COLOR_WHITE);
    hud_fill_rect(&s, 790, 470, 100, 100, HUD_COLOR_RED);  /* clipping check */

    FILE *f = fopen("hud_preview.ppm", "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", VIS_W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < VIS_W; x++)
        {
            uint16_t p = fb[y * W + x];
            unsigned char rgb[3] = { (unsigned char)((p >> 8) & 0xF8),
                                     (unsigned char)((p >> 3) & 0xFC),
                                     (unsigned char)((p << 3) & 0xF8) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("wrote hud_preview.ppm\n");
}

int main(void)
{
    test_static_tilt();
    test_gyro_tracking();
    test_impact_no_fall();
    test_jump_landing();
    test_fall();
    test_get_up();
    test_format();
    write_preview();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
