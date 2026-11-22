/*******************************************************************************
* File Name        : test_radar.c
*
* Description      : Host (PC) test of the radar gesture pipeline without
*                    hardware: synthesizes BGT60TR13C FIFO frames (64 samples
*                    x 32 chirps x 3 RX, 12-bit, interleaved) of a hand
*                    moving in front of static clutter and checks that
*                    radar_dsp + radar_gesture report the expected gestures.
*                    Also checks the HUD page logic and writes a preview of
*                    the HUD panel (radar_hud_preview.ppm, 2x2 pages).
*
*                    The signal model is idealized (point target, lambda/2
*                    antennas, white noise). Passing here does NOT prove the
*                    thresholds work with a real gloved hand.
*
*                    make -C tools/radar_host_test run
*
*******************************************************************************/
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "radar_config.h"
#include "radar_dsp.h"
#include "radar_gesture.h"
#include "radar_hud.h"

#define PI_D        (3.14159265358979)
#define FRAME_S     (0.0300446)

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        checks++;                                           \
        if (!(cond)) {                                      \
            failures++;                                     \
            printf("  FAIL: " __VA_ARGS__);                 \
            printf("\n");                                   \
        }                                                   \
    } while (0)

/*------------------------------------------------------------------------------
* Synthetic radar
*-----------------------------------------------------------------------------*/
typedef struct
{
    int    present;
    double range_cm;
    double az_deg;
    double el_deg;
    double vel_mps;     /* radial, negative = approaching */
    double amp;         /* ADC LSB */
} target_t;

static uint32_t rng_state = 12345U;

static double urand(void)
{
    rng_state = (rng_state * 1103515245U) + 12345U;
    return ((double)((rng_state >> 8) & 0xFFFFFFU) + 0.5) / 16777216.0;
}

static double gauss(void)
{
    double u1 = urand();
    double u2 = urand();
    return sqrt(-2.0 * log(u1)) * cos(2.0 * PI_D * u2);
}

static void add_target(double* acc, const target_t* t)
{
    const double lambda = 2.99792458e8 / 60.5e9;
    const double kb = t->range_cm / (double)RADAR_DSP_BIN_CM;
    double ph_ant[RADAR_DSP_RX] = { 0.0, 0.0, 0.0 };

    ph_ant[RADAR_RX_AZ_A] += (double)RADAR_AZ_SIGN * PI_D * sin(t->az_deg * PI_D / 180.0);
    ph_ant[RADAR_RX_EL_A] += (double)RADAR_EL_SIGN * PI_D * sin(t->el_deg * PI_D / 180.0);

    for (uint32_t c = 0U; c < RADAR_DSP_CHIRPS; c++)
    {
        double r_m = (t->range_cm / 100.0) + (t->vel_mps * (double)c * (double)RADAR_DSP_CHIRP_TIME_S);
        double ph0 = 4.0 * PI_D * r_m / lambda;

        for (uint32_t s = 0U; s < RADAR_DSP_SAMPLES; s++)
        {
            for (uint32_t a = 0U; a < RADAR_DSP_RX; a++)
            {
                acc[((c * RADAR_DSP_SAMPLES) + s) * RADAR_DSP_RX + a] +=
                    t->amp * cos((2.0 * PI_D * kb * (double)s / (double)RADAR_DSP_SAMPLES) + ph0 + ph_ant[a]);
            }
        }
    }
}

/* Static clutter (goggle frame / face / helmet) + noise + optional hand */
static void make_frame(uint16_t* raw, const target_t* hand, double noise_lsb)
{
    static double acc[RADAR_DSP_FRAME_SAMPLES];
    const target_t clutter[] =
    {
        { 1, 3.0,  0.0,  0.0, 0.0, 300.0 },     /* leakage / goggle frame */
        { 1, 22.0, 10.0, -5.0, 0.0, 120.0 },    /* static object in window */
        { 1, 80.0, -20.0, 0.0, 0.0, 80.0 },     /* far background          */
    };

    for (uint32_t i = 0U; i < RADAR_DSP_FRAME_SAMPLES; i++)
    {
        acc[i] = 2048.0;
    }
    for (size_t i = 0U; i < (sizeof(clutter) / sizeof(clutter[0])); i++)
    {
        add_target(acc, &clutter[i]);
    }
    if ((NULL != hand) && hand->present)
    {
        add_target(acc, hand);
    }
    for (uint32_t i = 0U; i < RADAR_DSP_FRAME_SAMPLES; i++)
    {
        double v = acc[i] + (noise_lsb * gauss());
        if (v < 0.0) { v = 0.0; }
        if (v > 4095.0) { v = 4095.0; }
        raw[i] = (uint16_t)(v + 0.5);
    }
}

/*------------------------------------------------------------------------------
* Scenario runner: idle frames, then a gesture track, then idle frames.
*-----------------------------------------------------------------------------*/
typedef struct
{
    const char* name;
    uint32_t frames;            /* gesture length in frames */
    target_t start;
    target_t end;
    radar_gesture_t expect;
    double jitter_deg;          /* per-frame angle jitter (extended hand)   */
} scenario_t;

static uint16_t raw[RADAR_DSP_FRAME_SAMPLES];

static int run_scenario(const scenario_t* sc, int verbose)
{
    radar_dsp_t dsp;
    radar_gesture_ctx_t gc;
    radar_frame_result_t res;
    radar_gesture_t got = RADAR_GESTURE_NONE;
    int n_gestures = 0;

    radar_dsp_init(&dsp);
    radar_gesture_init(&gc);

    for (uint32_t f = 0U; f < (40U + sc->frames + 40U); f++)
    {
        target_t hand = { 0 };
        radar_gesture_t g;

        if ((f >= 40U) && (f < (40U + sc->frames)))
        {
            double u = (sc->frames > 1U) ? (double)(f - 40U) / (double)(sc->frames - 1U) : 0.0;
            hand.present  = 1;
            hand.range_cm = sc->start.range_cm + (u * (sc->end.range_cm - sc->start.range_cm));
            hand.az_deg   = sc->start.az_deg + (u * (sc->end.az_deg - sc->start.az_deg));
            hand.el_deg   = sc->start.el_deg + (u * (sc->end.el_deg - sc->start.el_deg));
            hand.vel_mps  = sc->start.vel_mps;
            hand.amp      = sc->start.amp * (1.0 + (0.3 * (urand() - 0.5)));
            hand.az_deg  += sc->jitter_deg * gauss();
            hand.el_deg  += sc->jitter_deg * gauss();
        }
        make_frame(raw, &hand, 2.0);
        radar_dsp_process(&dsp, raw, radar_gesture_clutter_mode(&gc), &res);
        if (verbose && (f >= 38U) && (f < (44U + sc->frames + 25U)))
        {
            printf("    f%3u pres=%d rng=%5.1f az=%6.1f el=%6.1f v=%5.2f E=%.3g N=%.3g\n",
                   f, res.present, res.range_cm, res.azimuth_deg, res.elevation_deg,
                   res.velocity_mps, res.energy, res.noise);
        }
        g = radar_gesture_update(&gc, &res);
        if (RADAR_GESTURE_NONE != g)
        {
            got = g;
            n_gestures++;
        }
    }

    printf("  %-28s expect %-12s got %-12s (d_az %+6.1f d_el %+6.1f d_rng %+6.1f, %u frames)\n",
           sc->name, radar_gesture_name(sc->expect), radar_gesture_name(got),
           gc.last_d_az, gc.last_d_el, gc.last_d_rng, gc.last_frames);
    CHECK(got == sc->expect, "%s: expected %s, got %s", sc->name,
          radar_gesture_name(sc->expect), radar_gesture_name(got));
    CHECK(n_gestures <= 1, "%s: %d gestures reported", sc->name, n_gestures);
    return (got == sc->expect);
}

/*------------------------------------------------------------------------------
* Single-frame accuracy: range / angles / velocity of a moving point target
*-----------------------------------------------------------------------------*/
static void test_single_frame(void)
{
    const target_t cases[] =
    {
        { 1, 15.0,  20.0,   0.0, -0.5, 200.0 },
        { 1, 30.0, -30.0,  10.0,  0.6, 150.0 },
        { 1, 40.0,   0.0, -25.0, -1.0, 150.0 },
    };

    printf("Single frame estimation\n");
    for (size_t i = 0U; i < (sizeof(cases) / sizeof(cases[0])); i++)
    {
        radar_dsp_t dsp;
        radar_frame_result_t res;

        radar_dsp_init(&dsp);
        for (int f = 0; f < 30; f++)            /* learn clutter */
        {
            make_frame(raw, NULL, 2.0);
            radar_dsp_process(&dsp, raw, RADAR_CLUTTER_LEARN, &res);
        }
        make_frame(raw, &cases[i], 2.0);
        radar_dsp_process(&dsp, raw, RADAR_CLUTTER_LEARN, &res);

        printf("  true r=%4.1f az=%+5.1f el=%+5.1f v=%+4.1f | est r=%4.1f az=%+5.1f el=%+5.1f v=%+5.2f present=%d\n",
               cases[i].range_cm, cases[i].az_deg, cases[i].el_deg, cases[i].vel_mps,
               res.range_cm, res.azimuth_deg, res.elevation_deg, res.velocity_mps, res.present);
        CHECK(res.present, "target %zu not detected", i);
        CHECK(fabsf(res.range_cm - (float)cases[i].range_cm) < 3.0f, "range %zu", i);
        CHECK(fabsf(res.azimuth_deg - (float)cases[i].az_deg) < 6.0f, "azimuth %zu", i);
        CHECK(fabsf(res.elevation_deg - (float)cases[i].el_deg) < 6.0f, "elevation %zu", i);
        CHECK(fabsf(res.velocity_mps - (float)cases[i].vel_mps) < 0.2f, "velocity %zu", i);
    }
}

static void test_no_target(void)
{
    radar_dsp_t dsp;
    radar_gesture_ctx_t gc;
    radar_frame_result_t res;
    int detections = 0;
    int gestures = 0;

    printf("Clutter + noise only (300 frames = 9 s)\n");
    radar_dsp_init(&dsp);
    radar_gesture_init(&gc);
    for (int f = 0; f < 300; f++)
    {
        make_frame(raw, NULL, 3.0);
        radar_dsp_process(&dsp, raw, radar_gesture_clutter_mode(&gc), &res);
        detections += res.present ? 1 : 0;
        gestures += (RADAR_GESTURE_NONE != radar_gesture_update(&gc, &res)) ? 1 : 0;
    }
    printf("  detections %d, gestures %d\n", detections, gestures);
    CHECK(0 == detections, "false detections: %d", detections);
    CHECK(0 == gestures, "false gestures: %d", gestures);
}

/* Swipe right, pause, swipe left, then the hand stays still and leaves:
 * expect exactly RIGHT then LEFT, and no gesture from the hovering hand. */
static void test_sequence(void)
{
    radar_dsp_t dsp;
    radar_gesture_ctx_t gc;
    radar_frame_result_t res;
    radar_gesture_t seen[8];
    int n = 0;

    printf("Sequence: swipe right, 0.6 s pause, swipe left, hover 3 s, leave\n");
    radar_dsp_init(&dsp);
    radar_gesture_init(&gc);
    for (uint32_t f = 0U; f < 330U; f++)
    {
        target_t hand = { 0 };
        radar_gesture_t g;

        if ((f >= 40U) && (f < 52U))            /* swipe right */
        {
            double u = (double)(f - 40U) / 11.0;
            hand = (target_t){ 1, 20.0, -45.0 + (90.0 * u), 0.0, 0.3, 150.0 };
        }
        else if ((f >= 72U) && (f < 84U))       /* swipe left */
        {
            double u = (double)(f - 72U) / 11.0;
            hand = (target_t){ 1, 22.0, 45.0 - (90.0 * u), 0.0, -0.3, 150.0 };
        }
        else if ((f >= 150U) && (f < 250U))     /* hover still, then leave */
        {
            hand = (target_t){ 1, 25.0, 5.0, 5.0, 0.0, 150.0 };
        }
        make_frame(raw, &hand, 2.0);
        radar_dsp_process(&dsp, raw, radar_gesture_clutter_mode(&gc), &res);
        g = radar_gesture_update(&gc, &res);
        if ((RADAR_GESTURE_NONE != g) && (n < 8))
        {
            printf("  frame %3u: %s\n", f, radar_gesture_name(g));
            seen[n++] = g;
        }
    }
    CHECK((n == 2) && (seen[0] == RADAR_GESTURE_SWIPE_RIGHT) && (seen[1] == RADAR_GESTURE_SWIPE_LEFT),
          "sequence: %d gestures", n);
}

/*------------------------------------------------------------------------------
* HUD
*-----------------------------------------------------------------------------*/
static void test_hud_pages(void)
{
    char buf[16];

    printf("HUD page logic\n");
    CHECK(HUD_PAGE_NAV   == radar_hud_next_page(HUD_PAGE_SPEED, RADAR_GESTURE_SWIPE_RIGHT), "right");
    CHECK(HUD_PAGE_SPEED == radar_hud_next_page(HUD_PAGE_RADAR, RADAR_GESTURE_SWIPE_RIGHT), "wrap right");
    CHECK(HUD_PAGE_RADAR == radar_hud_next_page(HUD_PAGE_SPEED, RADAR_GESTURE_SWIPE_LEFT), "wrap left");
    CHECK(HUD_PAGE_NAV   == radar_hud_next_page(HUD_PAGE_TEAM, RADAR_GESTURE_SWIPE_LEFT), "left");
    CHECK(HUD_PAGE_SPEED == radar_hud_next_page(HUD_PAGE_TEAM, RADAR_GESTURE_SWIPE_UP), "up = home");
    CHECK(HUD_PAGE_RADAR == radar_hud_next_page(HUD_PAGE_NAV, RADAR_GESTURE_SWIPE_DOWN), "down = radar");
    CHECK(HUD_PAGE_TEAM  == radar_hud_next_page(HUD_PAGE_TEAM, RADAR_GESTURE_PUSH), "push keeps page");
    CHECK(HUD_PAGE_TEAM  == radar_hud_next_page(HUD_PAGE_TEAM, RADAR_GESTURE_NONE), "none keeps page");

    (void)radar_format_fixed(buf, sizeof(buf), 23.45f, 1U, false);
    CHECK(0 == strcmp(buf, "23.5"), "fmt 23.5 got %s", buf);
    (void)radar_format_fixed(buf, sizeof(buf), -7.04f, 1U, true);
    CHECK(0 == strcmp(buf, "-7.0"), "fmt -7.0 got %s", buf);
    (void)radar_format_fixed(buf, sizeof(buf), 12.4f, 0U, true);
    CHECK(0 == strcmp(buf, "+12"), "fmt +12 got %s", buf);
    (void)radar_format_fixed(buf, sizeof(buf), -0.02f, 1U, true);
    CHECK(0 == strcmp(buf, "+0.0"), "fmt +0.0 got %s", buf);
}

static void write_preview(const char* path)
{
    const uint32_t gap = 12U;
    const uint32_t w = (2U * RADAR_HUD_WIDTH) + (3U * gap);
    const uint32_t h = (2U * RADAR_HUD_HEIGHT) + (3U * gap);
    uint16_t* fb = calloc((size_t)w * h, sizeof(uint16_t));
    FILE* fp;
    const radar_hud_view_t views[4] =
    {
        { HUD_PAGE_SPEED, RADAR_HUD_MODE_OK, false, RADAR_GESTURE_NONE, false, 0, 0, 0 },
        { HUD_PAGE_NAV,   RADAR_HUD_MODE_OK, true,  RADAR_GESTURE_SWIPE_RIGHT, false, 0, 0, 0 },
        { HUD_PAGE_TEAM,  RADAR_HUD_MODE_OK, true,  RADAR_GESTURE_SWIPE_LEFT, false, 0, 0, 0 },
        { HUD_PAGE_RADAR, RADAR_HUD_MODE_OK, false, RADAR_GESTURE_NONE, true, 23.4f, 12.0f, -3.0f },
    };

    if (NULL == fb)
    {
        return;
    }
    for (uint32_t i = 0U; i < ((uint32_t)w * h); i++)
    {
        fb[i] = RADAR_HUD_RGB565(40U, 40U, 48U);
    }
    for (uint32_t i = 0U; i < 4U; i++)
    {
        uint32_t x = gap + ((i % 2U) * (RADAR_HUD_WIDTH + gap));
        uint32_t y = gap + ((i / 2U) * (RADAR_HUD_HEIGHT + gap));
        radar_hud_draw(fb, w, h, x, y, &views[i]);
    }

    fp = fopen(path, "wb");
    if (NULL != fp)
    {
        fprintf(fp, "P6\n%u %u\n255\n", w, h);
        for (uint32_t i = 0U; i < ((uint32_t)w * h); i++)
        {
            uint8_t rgb[3];
            rgb[0] = (uint8_t)(((fb[i] >> 11) & 0x1FU) << 3);
            rgb[1] = (uint8_t)(((fb[i] >> 5) & 0x3FU) << 2);
            rgb[2] = (uint8_t)((fb[i] & 0x1FU) << 3);
            (void)fwrite(rgb, 1U, 3U, fp);
        }
        fclose(fp);
        printf("Wrote %s (%ux%u)\n", path, w, h);
    }
    free(fb);
}

int main(int argc, char** argv)
{
    const int verbose = (argc > 1) && (0 == strcmp(argv[1], "-v"));
    /* name, frames, start {present, range, az, el, v, amp}, end, expected */
    const scenario_t scenarios[] =
    {
        { "swipe right (gloved, 20cm)", 12U, { 1, 20.0, -45.0, 0.0, 0.3, 150.0 }, { 1, 20.0, 45.0, 0.0, 0.3, 150.0 }, RADAR_GESTURE_SWIPE_RIGHT, 0.0 },
        { "swipe left (gloved, 25cm)",  12U, { 1, 25.0,  45.0, 5.0, -0.3, 150.0 }, { 1, 25.0, -45.0, 5.0, -0.3, 150.0 }, RADAR_GESTURE_SWIPE_LEFT, 0.0 },
        { "swipe right fast (6 fr)",     6U, { 1, 18.0, -40.0, 0.0, 0.8, 200.0 }, { 1, 18.0, 40.0, 0.0, 0.8, 200.0 }, RADAR_GESTURE_SWIPE_RIGHT, 0.0 },
        { "swipe right slow (30 fr)",   30U, { 1, 30.0, -40.0, 0.0, 0.1, 120.0 }, { 1, 30.0, 40.0, 0.0, 0.1, 120.0 }, RADAR_GESTURE_SWIPE_RIGHT, 0.0 },
        { "swipe up",                   12U, { 1, 20.0, 0.0, -40.0, 0.3, 150.0 }, { 1, 20.0, 0.0, 40.0, 0.3, 150.0 }, RADAR_GESTURE_SWIPE_UP, 0.0 },
        { "swipe down",                 12U, { 1, 20.0, 5.0, 40.0, -0.3, 150.0 }, { 1, 20.0, 5.0, -40.0, -0.3, 150.0 }, RADAR_GESTURE_SWIPE_DOWN, 0.0 },
        { "push 35->15 cm",             12U, { 1, 35.0, 0.0, 0.0, -0.55, 150.0 }, { 1, 15.0, 5.0, -5.0, -0.55, 150.0 }, RADAR_GESTURE_PUSH, 0.0 },
        { "hand hovers still (no gest)",40U, { 1, 20.0, 5.0, 5.0, 0.0, 150.0 }, { 1, 20.0, 5.0, 5.0, 0.0, 150.0 }, RADAR_GESTURE_NONE, 0.0 },
        { "small wiggle (no gesture)",  12U, { 1, 22.0, -8.0, 0.0, 0.2, 150.0 }, { 1, 22.0, 8.0, 3.0, 0.2, 150.0 }, RADAR_GESTURE_NONE, 0.0 },
        { "far hand 60cm (ignored)",    12U, { 1, 60.0, -45.0, 0.0, 0.3, 150.0 }, { 1, 60.0, 45.0, 0.0, 0.3, 150.0 }, RADAR_GESTURE_NONE, 0.0 },
        { "swipe right + 8deg jitter",  12U, { 1, 20.0, -45.0, 0.0, 0.3, 150.0 }, { 1, 20.0, 45.0, 0.0, 0.3, 150.0 }, RADAR_GESTURE_SWIPE_RIGHT, 8.0 },
        { "swipe left + 8deg jitter",   12U, { 1, 25.0, 45.0, 0.0, -0.3, 150.0 }, { 1, 25.0, -45.0, 0.0, -0.3, 150.0 }, RADAR_GESTURE_SWIPE_LEFT, 8.0 },
        { "push + 8deg jitter",         12U, { 1, 35.0, 0.0, 0.0, -0.55, 150.0 }, { 1, 15.0, 0.0, 0.0, -0.55, 150.0 }, RADAR_GESTURE_PUSH, 8.0 },
    };

    printf("Radar gesture host test (bin = %.2f cm)\n", (double)RADAR_DSP_BIN_CM);
    test_single_frame();
    test_no_target();

    printf("Gesture scenarios\n");
    for (size_t i = 0U; i < (sizeof(scenarios) / sizeof(scenarios[0])); i++)
    {
        (void)run_scenario(&scenarios[i], verbose);
    }

    test_sequence();
    test_hud_pages();
    write_preview("radar_hud_preview.ppm");

    printf("\n%d / %d checks passed\n", checks - failures, checks);
    return (0 == failures) ? 0 : 1;
}
