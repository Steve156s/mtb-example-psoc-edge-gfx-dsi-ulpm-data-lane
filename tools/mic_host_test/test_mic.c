/*
 * Host unit test for proj_cm55/mic/mic_level.c and mic_hud.c
 * Build & run:  make
 * Writes mic_hud_preview.ppm (VU panel preview).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mic_level.h"
#include "mic_hud.h"

#define FS      (16000.0)
#ifndef M_PI
#define M_PI    (3.14159265358979323846)
#endif
#define N       (160U)

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double phase = 0.0;
static unsigned rng = 12345U;

static double noise(void)
{
    rng = rng * 1103515245U + 12345U;
    return ((double)((rng >> 8) & 0xFFFF) / 32768.0) - 1.0;
}

/* amplitude relative to full scale, plus DC offset in counts */
static void gen(int16_t *buf, double amp, double freq, double dc, double noise_amp)
{
    for (unsigned i = 0; i < N; i++)
    {
        double v = (amp * sin(phase) + noise_amp * noise()) * 32767.0 + dc;
        phase += 2.0 * M_PI * freq / FS;
        if (v > 32767.0) v = 32767.0;
        if (v < -32768.0) v = -32768.0;
        buf[i] = (int16_t)lrint(v);
    }
}

static void test_db_conversion(void)
{
    /* full-scale square: ms = 32768^2 = 2^30 -> 0 dBFS */
    int16_t d = mic_level_ms_to_db10(1ULL << 30);
    CHECK(d == 0, "2^30 -> %d (want 0)", d);
    d = mic_level_ms_to_db10((1ULL << 30) / 100ULL);
    CHECK(abs(d + 200) <= 1, "-20 dB -> %d", d);
    d = mic_level_ms_to_db10(1U);
    CHECK(abs(d + 903) <= 1, "ms=1 -> %d (want -903)", d);
    d = mic_level_ms_to_db10(0U);
    CHECK(d == MIC_LEVEL_FLOOR_DB10, "ms=0 -> %d", d);

    /* sweep against libm */
    for (double db = -75.0; db <= 0.0; db += 7.3)
    {
        uint64_t ms = (uint64_t)llround(pow(10.0, db / 10.0) * 1073741824.0);
        int16_t got = mic_level_ms_to_db10(ms);
        CHECK(fabs(got - db * 10.0) <= 1.5, "sweep %.1f dB -> %d", db, got);
    }
}

static void test_level_sine_and_dc(void)
{
    mic_level_t lv;
    int16_t buf[N];

    mic_level_init(&lv, -720, 15, 150, 32000);
    phase = 0.0;
    /* -20 dBFS peak sine (0.1 amplitude) on top of a large DC offset */
    for (int f = 0; f < 200; f++)
    {
        gen(buf, 0.1, 1000.0, 8000.0, 0.0);
        mic_level_process(&lv, buf, N);
    }
    /* sine RMS = peak - 3.01 dB -> -23.0 dBFS */
    CHECK(abs(lv.rms_db10 + 230) <= 5, "sine rms %d (want -230)", lv.rms_db10);
    CHECK(abs(lv.peak_db10 + 200) <= 5, "sine peak %d (want -200)", lv.peak_db10);
    CHECK(lv.clip_count == 0U, "no clip expected, got %u", lv.clip_count);

    /* DC must be gone from the output */
    long sum = 0;
    for (unsigned i = 0; i < N; i++) sum += buf[i];
    CHECK(labs(sum / (long)N) < 40, "residual DC %ld", sum / (long)N);

    /* VU release: go silent, bar must fall 1.5 dB per frame, not jump */
    int16_t before = lv.vu_db10;
    /* 'silence' = the same DC offset without the tone */
    for (unsigned i = 0; i < N; i++) buf[i] = 8000;
    mic_level_process(&lv, buf, N);
    CHECK(lv.vu_db10 == before - 15, "release %d -> %d", before, lv.vu_db10);

    /* clipping */
    gen(buf, 1.2, 500.0, 0.0, 0.0);
    mic_level_process(&lv, buf, N);
    CHECK(lv.clip_count > 0U, "clip not counted");

    /* fill mapping */
    CHECK(mic_level_vu_fill(&lv, 0, 240) == 240, "fill at 0 dB");
    CHECK(mic_level_vu_fill(&lv, -720, 240) == 0, "fill at min");
    CHECK(mic_level_vu_fill(&lv, -360, 240) == 120, "fill at half");
}

static void test_vad(void)
{
    mic_level_t lv;
    mic_vad_t vad;
    int16_t buf[N];
    int start_frame = -1, end_frame = -1, starts = 0;

    mic_level_init(&lv, -720, 15, 150, 32000);
    mic_vad_init(&vad, 120, 60, -600, 3, 80);
    phase = 0.0;

    for (int f = 0; f < 600; f++)
    {
        /* background noise ~ -55 dBFS, a 'voice' burst from 2.0 s to 3.0 s */
        double amp = (f >= 200 && f < 300) ? 0.05 : 0.0;
        gen(buf, amp, 300.0, 1500.0, 0.003);
        mic_level_process(&lv, buf, N);
        mic_vad_event_t ev = mic_vad_process(&vad, lv.rms_db10);
        if (ev == MIC_VAD_EVENT_START) { starts++; if (start_frame < 0) start_frame = f; }
        if (ev == MIC_VAD_EVENT_END && end_frame < 0) end_frame = f;
    }
    printf("  VAD: floor %d dB*10, start @%d, end @%d, starts=%d\n",
           mic_vad_floor_db10(&vad), start_frame, end_frame, starts);
    CHECK(starts == 1, "expected exactly one START, got %d", starts);
    CHECK(start_frame >= 200 && start_frame <= 205, "start frame %d", start_frame);
    CHECK(end_frame >= 300 + 79 && end_frame <= 300 + 82, "end frame %d", end_frame);

    /* slow loudness drift (e.g. wind building up) must not trigger */
    mic_vad_init(&vad, 120, 60, -600, 3, 80);
    starts = 0;
    for (int f = 0; f < 3000; f++)
    {
        double na = 0.002 * pow(10.0, (f / 3000.0) * 20.0 / 20.0);  /* +20 dB over 30 s */
        gen(buf, 0.0, 300.0, 0.0, na);
        mic_level_process(&lv, buf, N);
        if (mic_vad_process(&vad, lv.rms_db10) == MIC_VAD_EVENT_START) starts++;
    }
    CHECK(starts == 0, "slow drift triggered %d times", starts);
}

static void write_preview(void)
{
    enum { W = 420, H = 170 };
    static uint16_t fb[W * H];
    mic_hud_view_t v;
    const char *labels[4] = { "D-L", "D-R", "A-1", "A-2" };
    const uint32_t fills[4] = { 180, 150, 90, 230 };

    memset(fb, 0, sizeof(fb));
    memset(&v, 0, sizeof(v));
    for (int i = 0; i < 4; i++)
    {
        v.ch[i].label = labels[i];
        v.ch[i].status = MIC_HUD_CH_OK;
        v.ch[i].vu_fill = fills[i];
        v.ch[i].hold_fill = fills[i] + 8;
        v.ch[i].rms_db10 = (int16_t)(-720 + (int)fills[i] * 3);
    }
    v.ch[3].clipping = true;
    v.wake_mode = true;
    v.vad_active = true;
    v.floor_db10 = -552;
    mic_hud_draw(fb, W, W, H, 10, 10, &v);

    FILE *fp = fopen("mic_hud_preview.ppm", "wb");
    if (!fp) return;
    fprintf(fp, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++)
    {
        unsigned char rgb[3] = {
            (unsigned char)(((fb[i] >> 11) & 0x1F) << 3),
            (unsigned char)(((fb[i] >> 5) & 0x3F) << 2),
            (unsigned char)((fb[i] & 0x1F) << 3) };
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
    printf("  wrote mic_hud_preview.ppm\n");
}

int main(void)
{
    printf("mic_level / mic_hud host test\n");
    test_db_conversion();
    test_level_sine_and_dc();
    test_vad();
    write_preview();
    if (failures)
    {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
