/* Host unit test of the portable audio code: TLV320DAC3100 clock search and
 * register sequence, tone synthesizer, IMA ADPCM, number words, sequences. */
#define _USE_MATH_DEFINES
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tlv320dac3100.h"
#include "audio_synth.h"
#include "audio_adpcm.h"
#include "audio_seq.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- fake bus */
typedef struct { uint8_t page, reg, val; } wr_t;
static wr_t log_[512];
static int nlog;
static uint8_t regs[2][128];
static uint8_t cur_page;
static int fail_writes;

static int fake_write(void* ctx, uint8_t addr, const uint8_t* d, uint16_t len)
{
    (void)ctx;
    if ((addr != TLV320_I2C_ADDR) || (len != 2U) || fail_writes) { return -1; }
    if (d[0] == 0U) { cur_page = d[1]; }
    if ((d[0] == 1U) && (cur_page == 0U) && (d[1] & 1U)) { cur_page = 0; memset(regs, 0, sizeof(regs)); }
    if (cur_page < 2U) { regs[cur_page][d[0] & 0x7FU] = d[1]; }
    if (nlog < 512) { log_[nlog].page = cur_page; log_[nlog].reg = d[0]; log_[nlog].val = d[1]; nlog++; }
    return 0;
}
static int fake_read(void* ctx, uint8_t addr, uint8_t reg, uint8_t* v)
{
    (void)ctx; (void)addr;
    *v = (cur_page < 2U) ? regs[cur_page][reg & 0x7FU] : 0U;
    return 0;
}
static uint32_t delayed;
static void fake_delay(void* ctx, uint32_t ms) { (void)ctx; delayed += ms; }
static const tlv320_bus_t fake_bus = { fake_write, fake_read, fake_delay, NULL };

static int find(uint8_t page, uint8_t reg)   /* index of the last write */
{
    int i, at = -1;
    for (i = 0; i < nlog; i++) { if ((log_[i].page == page) && (log_[i].reg == reg)) { at = i; } }
    return at;
}
static int first(uint8_t page, uint8_t reg)
{
    int i;
    for (i = 0; i < nlog; i++) { if ((log_[i].page == page) && (log_[i].reg == reg)) { return i; } }
    return -1;
}

/* ---------------------------------------------------------------- clocks */
static void check_clock_rules(const tlv320_clock_t* c, uint32_t mclk, uint32_t fs, int strict)
{
    uint64_t clkin = c->use_pll ? ((uint64_t)mclk * c->pll_r * c->pll_j) / c->pll_p : mclk;
    CHECK(clkin == c->codec_clkin_hz, "clkin");
    CHECK((uint64_t)c->ndac * c->mdac * c->dosr * fs == clkin, "exact fs %u", (unsigned)fs);
    CHECK(c->fs_hz == fs, "fs field");
    CHECK(c->dac_clk_hz <= 49152000UL, "DAC_CLK %lu", (unsigned long)c->dac_clk_hz);
    if (c->use_pll)
    {
        CHECK(clkin >= 80000000ULL && clkin <= 110000000ULL, "PLL range %llu", (unsigned long long)clkin);
        CHECK(mclk / c->pll_p >= 512000UL && mclk / c->pll_p <= 20000000UL, "PLL in");
        CHECK(c->pll_j >= 4 && c->pll_j <= 63 && c->pll_d == 0, "J/D");
        CHECK(c->prb == 25, "PRB_P25");
    }
    if (strict)
    {
        CHECK(c->dac_mod_clk_hz >= 2800000UL && c->dac_mod_clk_hz <= 6200000UL, "DAC_MOD %lu", (unsigned long)c->dac_mod_clk_hz);
        CHECK(c->dosr % 8 == 0, "DOSR multiple of 8");
        CHECK((uint32_t)c->mdac * c->dosr / 32U >= 12U, "resource class");
    }
}

static void test_clock(void)
{
    tlv320_clock_t c;
    static const uint32_t mclks[] = { 2048000, 4096000, 6144000, 12288000, 12000000, 24576000 };
    static const uint32_t rates[] = { 8000, 16000, 32000, 48000 };
    size_t i, j;

    CHECK(TLV320_OK == tlv320_clock_calc(2048000, 16000, true, &c), "pll calc");
    check_clock_rules(&c, 2048000, 16000, 1);
    printf("  PLL  2.048 MHz -> P=%u R=%u J=%u PLL=%lu NDAC=%u MDAC=%u DOSR=%u DAC_MOD=%lu fs=%lu\n",
           c.pll_p, c.pll_r, c.pll_j, (unsigned long)c.codec_clkin_hz, c.ndac, c.mdac, c.dosr,
           (unsigned long)c.dac_mod_clk_hz, (unsigned long)c.fs_hz);

    CHECK(TLV320_OK == tlv320_clock_calc(2048000, 16000, false, &c), "direct calc");
    CHECK(c.ndac == 1 && c.mdac == 1 && c.dosr == 128 && c.prb == 1, "direct = Infineon setting");
    check_clock_rules(&c, 2048000, 16000, 0);

    for (i = 0; i < sizeof(mclks) / sizeof(mclks[0]); i++)
    {
        for (j = 0; j < sizeof(rates) / sizeof(rates[0]); j++)
        {
            if (TLV320_OK == tlv320_clock_calc(mclks[i], rates[j], true, &c))
            {
                check_clock_rules(&c, mclks[i], rates[j], 1);
            }
            else
            {
                CHECK(0, "no PLL setting for MCLK %u fs %u", (unsigned)mclks[i], (unsigned)rates[j]);
            }
        }
    }
    CHECK(TLV320_ERR_CLOCK == tlv320_clock_calc(2048000, 44100, false, &c), "44.1k from 2.048M direct impossible");
    CHECK(TLV320_ERR_PARAM == tlv320_clock_calc(0, 16000, true, &c), "param");
}

/* ---------------------------------------------------------------- driver */
static void test_driver(void)
{
    tlv320_t dev;
    tlv320_clock_t c;
    tlv320_status_t st;

    (void)tlv320_clock_calc(2048000, 16000, true, &c);
    nlog = 0; delayed = 0; cur_page = 7; memset(regs, 0xAA, sizeof(regs));
    CHECK(TLV320_OK == tlv320_init(&dev, &fake_bus, &c, TLV320_OUT_SPEAKER | TLV320_OUT_HEADPHONE, 6), "init");

    CHECK(log_[0].reg == 0 && log_[0].val == 0, "page 0 first");
    CHECK(log_[1].reg == 1 && log_[1].val == 1, "reset second");
    CHECK(regs[0][4] == 0x03, "clk mux PLL");
    CHECK(regs[0][5] == (uint8_t)(0x80 | (c.pll_p << 4) | c.pll_r), "PLL PR %02X", regs[0][5]);
    CHECK(regs[0][6] == c.pll_j && regs[0][7] == 0 && regs[0][8] == 0, "J D");
    CHECK(find(0, 6) < find(0, 5), "J before PLL power");
    CHECK(find(0, 5) < first(0, 11), "PLL before NDAC");
    CHECK(regs[0][11] == (0x80 | c.ndac) && regs[0][12] == (0x80 | c.mdac), "NDAC MDAC");
    CHECK(((regs[0][13] << 8) | regs[0][14]) == c.dosr, "DOSR");
    CHECK(regs[0][27] == 0x00 && regs[0][60] == 25, "I2S 16 bit, PRB_P25");
    CHECK(regs[1][35] == 0x44 && regs[1][33] == 0x4E, "mixer, depop");
    CHECK(regs[1][40] == 0x06 && regs[1][41] == 0x06 && regs[1][31] == 0xC4, "HP");
    CHECK(first(1, 40) < find(1, 31), "HP driver gain before power");
    CHECK(regs[1][42] == 0x04 && regs[1][32] == 0x86 && regs[1][38] == 0x80, "SPK 6 dB on");
    CHECK(regs[0][63] == 0xD4 && regs[0][64] == 0x0C, "DAC on, muted");
    CHECK(find(1, 32) < find(0, 63), "outputs before DAC power");
    CHECK(delayed >= 12U, "reset + PLL delays");

    CHECK(TLV320_OK == tlv320_set_volume_half_db(&dev, -12), "vol");
    CHECK(regs[0][65] == 0xF4 && regs[0][66] == 0xF4, "-6 dB = 0xF4 (%02X)", regs[0][65]);
    (void)tlv320_set_volume_half_db(&dev, 100);
    CHECK(regs[0][65] == 48, "clamp +24 dB");
    (void)tlv320_set_volume_half_db(&dev, -500);
    CHECK(regs[0][65] == 0x81, "clamp -63.5 dB");
    (void)tlv320_mute(&dev, false);
    CHECK(regs[0][64] == 0x00, "unmute");
    (void)tlv320_speaker_power(&dev, false);
    CHECK(regs[1][32] == 0x06, "spk off");

    {
        int16_t sn = audio_sin_q15(audio_phase_inc(1000, 16000));
        int16_t cs = audio_sin_q15(audio_phase_inc(1000, 16000) + 0x40000000UL);
        CHECK(TLV320_OK == tlv320_beep(&dev, sn, cs, 16000, 6), "beep");
        CHECK(regs[0][73] == 0x00 && regs[0][74] == 0x3E && regs[0][75] == 0x80, "beep length");
        CHECK(regs[0][71] == (0x80 | 6) && regs[0][72] == 0x40, "beep enable last");
        CHECK(find(0, 71) > find(0, 79), "enable after coefficients");
        CHECK(abs(((regs[0][76] << 8) | regs[0][77]) - 12539) < 40, "sin(2pi/16)");
    }

    regs[0][3] = 0x00; regs[0][37] = 0xB2; regs[0][44] = 0x80; regs[1][32] = 0x87;
    CHECK(TLV320_OK == tlv320_read_status(&dev, &st), "status");
    CHECK(st.over_temperature && st.hp_short && st.spk_short && st.ldac_on && st.spk_on && st.hpl_on && st.hpr_on && !st.rdac_on, "status bits");

    CHECK(TLV320_OK == tlv320_power_down(&dev), "power down");
    CHECK(regs[0][11] == c.ndac && (regs[0][5] & 0x80) == 0 && regs[1][31] == 0x04, "dividers / PLL / HP off");
    CHECK(TLV320_ERR_STATE == tlv320_mute(&dev, true), "not ready after power down");

    /* direct clock: no PLL, no beep */
    (void)tlv320_clock_calc(2048000, 16000, false, &c);
    nlog = 0;
    CHECK(TLV320_OK == tlv320_init(&dev, &fake_bus, &c, TLV320_OUT_SPEAKER, 24), "init direct");
    CHECK(regs[0][4] == 0 && find(0, 5) < 0 && regs[0][60] == 1 && regs[1][42] == 0x1C, "direct regs");
    CHECK(find(1, 31) == -1 || regs[1][31] == 0x04, "HP stays off");
    CHECK(TLV320_ERR_PARAM == tlv320_beep(&dev, 1, 1, 10, 0), "no beep on PRB_P1");

    fail_writes = 1;
    CHECK(TLV320_ERR_BUS == tlv320_init(&dev, &fake_bus, &c, TLV320_OUT_SPEAKER, 6), "bus error");
    fail_writes = 0;
    CHECK(TLV320_ERR_PARAM == tlv320_init(&dev, &fake_bus, &c, 0, 6), "no output");
}

/* ---------------------------------------------------------------- synth */
static void test_synth(void)
{
    static int16_t buf[16000];
    audio_synth_t s;
    audio_note_t n1k = { 1000, 1000, 500, 16384 };
    size_t got, i;
    int crossings = 0, peak = 0;
    int t;

    audio_synth_start(&s, &n1k, 1, 16000);
    got = audio_synth_render(&s, buf, 16000);
    CHECK(got == 8000, "500 ms = 8000 samples (%zu)", got);
    for (i = 1; i < got; i++)
    {
        if ((buf[i - 1] < 0) && (buf[i] >= 0)) { crossings++; }
        if (abs(buf[i]) > peak) { peak = abs(buf[i]); }
    }
    CHECK(crossings >= 498 && crossings <= 501, "1 kHz -> %d cycles", crossings);
    CHECK(peak <= 16384 && peak > 16000, "peak %d", peak);
    CHECK(abs(buf[0]) < 100 && abs(buf[got - 1]) < 300, "click-free ends %d %d", buf[0], buf[got - 1]);

    for (t = 0; t < AUDIO_TONE_COUNT; t++)
    {
        uint32_t len = audio_synth_length(audio_tones[t].notes, audio_tones[t].count, 16000);
        size_t total = 0, k;
        int pk = 0;
        audio_synth_start(&s, audio_tones[t].notes, audio_tones[t].count, 16000);
        while ((k = audio_synth_render(&s, buf, 37)) > 0)   /* odd chunk size */
        {
            for (i = 0; i < k; i++) { if (abs(buf[i]) > pk) { pk = abs(buf[i]); } }
            total += k;
        }
        CHECK(total == len && len > 0, "%s length", audio_tones[t].name);
        CHECK(pk > 4000 && pk <= 23170, "%s peak %d", audio_tones[t].name, pk);
    }
    CHECK(abs(audio_sin_q15(0x40000000UL) - 32767) <= 1 && abs(audio_sin_q15(0xC0000000UL) + 32767) <= 1, "sin LUT");
}

/* ---------------------------------------------------------------- ADPCM */
static void test_adpcm(void)
{
    enum { N = 8000 };
    static int16_t in[N], out[N];
    static uint8_t enc[N / 2];
    audio_adpcm_t d;
    double sig = 0, err = 0;
    size_t i, got = 0, k;
    int c;

    for (i = 0; i < N; i++)
    {
        double f = 200.0 + 3000.0 * (double)i / N;     /* chirp */
        in[i] = (int16_t)(20000.0 * sin(2.0 * M_PI * f * (double)i / 16000.0));
    }
    audio_adpcm_encode(in, N, enc);
    audio_adpcm_start(&d, enc, N);
    while ((k = audio_adpcm_decode(&d, &out[got], 101)) > 0) { got += k; }
    CHECK(got == N, "decoded all");
    /* 4-bit IMA on a full-scale 0.2..3.2 kHz chirp: about 19 dB */
    for (i = 200; i < N; i++) { sig += (double)in[i] * in[i]; err += (double)(in[i] - out[i]) * (in[i] - out[i]); }
    printf("  ADPCM chirp SNR %.1f dB\n", 10.0 * log10(sig / err));
    CHECK(10.0 * log10(sig / err) > 15.0, "ADPCM SNR %.1f dB", 10.0 * log10(sig / err));

    for (c = 0; c < AUDIO_CLIP_COUNT; c++)
    {
        int pk = 0;
        size_t total = 0;
        audio_adpcm_start(&d, audio_clips[c].data, audio_clips[c].samples);
        while ((k = audio_adpcm_decode(&d, out, N)) > 0)
        {
            for (i = 0; i < k; i++) { if (abs(out[i]) > pk) { pk = abs(out[i]); } }
            total += k;
        }
        CHECK(total == audio_clips[c].samples, "clip %s length", audio_clips[c].name);
        CHECK(pk > 15000 && pk < 32767, "clip %s peak %d", audio_clips[c].name, pk);
    }
}

/* ---------------------------------------------------------------- words */
static const char* word(uint16_t id)
{
    static const char* w[] = { "零", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十", "百", "千", "兩" };
    if (id >= AUDIO_CLIP_D_0 && id <= AUDIO_CLIP_D_LIANG) { return w[id - AUDIO_CLIP_D_0]; }
    return "?";
}

static void test_words(void)
{
    static const struct { uint32_t n; const char* s; } cases[] =
    {
        { 0, "零" }, { 2, "二" }, { 7, "七" }, { 10, "十" }, { 12, "十二" }, { 20, "二十" },
        { 32, "三十二" }, { 100, "一百" }, { 105, "一百零五" }, { 110, "一百一十" },
        { 200, "兩百" }, { 250, "兩百五十" }, { 1000, "一千" }, { 1005, "一千零五" },
        { 1050, "一千零五十" }, { 1500, "一千五百" }, { 2010, "兩千零一十" },
        { 2350, "兩千三百五十" }, { 3008, "三千零八" }, { 9999, "九千九百九十九" }, { 85, "八十五" },
    };
    uint16_t ids[8];
    char s[64];
    size_t i, k, j;
    audio_seq_t q;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        k = audio_number_words(cases[i].n, ids, 8);
        s[0] = 0;
        for (j = 0; j < k; j++) { strcat(s, word(ids[j])); }
        CHECK(strcmp(s, cases[i].s) == 0, "%u -> %s (want %s)", (unsigned)cases[i].n, s, cases[i].s);
    }
    CHECK(audio_number_words(10000, ids, 8) == 0, "range");
    CHECK(audio_number_words(2350, ids, 3) == 0, "max too small");

    CHECK(audio_heading_clip(0) == AUDIO_CLIP_DIR_N && audio_heading_clip(359) == AUDIO_CLIP_DIR_N, "N");
    CHECK(audio_heading_clip(22) == AUDIO_CLIP_DIR_N && audio_heading_clip(23) == AUDIO_CLIP_DIR_NE, "N/NE edge");
    CHECK(audio_heading_clip(90) == AUDIO_CLIP_DIR_E && audio_heading_clip(180) == AUDIO_CLIP_DIR_S, "E S");
    CHECK(audio_heading_clip(225) == AUDIO_CLIP_DIR_SW && audio_heading_clip(315) == AUDIO_CLIP_DIR_NW, "SW NW");
    CHECK(audio_heading_clip(720 + 45) == AUDIO_CLIP_DIR_NE, "wrap");

    audio_seq_clear(&q);
    CHECK(audio_seq_speed_kmh(&q, 32) && q.count == 5 && q.seg[0].arg == AUDIO_CLIP_W_SPEED && q.seg[4].arg == AUDIO_CLIP_W_KM, "speed seq");
    audio_seq_clear(&q);
    CHECK(audio_seq_battery_pct(&q, 85) && q.count == 5 && q.seg[1].arg == AUDIO_CLIP_W_PERCENT, "battery seq");
    audio_seq_clear(&q);
    CHECK(audio_seq_altitude_m(&q, -30) && q.seg[1].arg == AUDIO_CLIP_D_0, "negative altitude -> 0");
    audio_seq_clear(&q);
    for (i = 0; i < 13; i++) { (void)audio_seq_add(&q, AUDIO_SEG_SILENCE, 1); }
    CHECK(!audio_seq_speed_kmh(&q, 2350) && q.count == 13, "full: nothing added");
}

/* ---------------------------------------------------------------- source */
static void test_source(void)
{
    static int16_t a[60000], b[60000];
    audio_seq_t q;
    audio_source_t s;
    size_t na = 0, nb = 0, k;
    uint32_t expect;

    audio_seq_clear(&q);
    (void)audio_seq_add(&q, AUDIO_SEG_TONE, AUDIO_TONE_CONFIRM);
    (void)audio_seq_add(&q, AUDIO_SEG_CLIP, AUDIO_CLIP_BOOT);
    (void)audio_seq_add(&q, AUDIO_SEG_SILENCE, 100);
    (void)audio_seq_add(&q, AUDIO_SEG_CLIP, AUDIO_CLIP_D_5);
    (void)audio_seq_add(&q, AUDIO_SEG_CLIP, AUDIO_CLIP_W_KM);

    expect = audio_synth_length(audio_tones[AUDIO_TONE_CONFIRM].notes, audio_tones[AUDIO_TONE_CONFIRM].count, 16000)
           + 640U /* gap */ + audio_clips[AUDIO_CLIP_BOOT].samples + 1600U
           + audio_clips[AUDIO_CLIP_D_5].samples + 640U + audio_clips[AUDIO_CLIP_W_KM].samples;

    audio_source_start(&s, &q, 16000, 40);
    na = audio_source_read(&s, a, 60000);
    CHECK(audio_source_done(&s), "done");
    CHECK(na == expect, "length %zu want %u", na, (unsigned)expect);

    audio_source_start(&s, &q, 16000, 40);
    while ((k = audio_source_read(&s, &b[nb], 256)) > 0) { nb += k; if (k < 256) { break; } }
    CHECK(nb == na && memcmp(a, b, na * sizeof(int16_t)) == 0, "block-wise == one-shot");

    audio_seq_clear(&q);
    (void)audio_seq_add(&q, AUDIO_SEG_CLIP, 999);   /* bad id: skipped */
    (void)audio_seq_add(&q, AUDIO_SEG_TONE, 99);
    audio_source_start(&s, &q, 16000, 40);
    k = audio_source_read(&s, a, 1000);
    CHECK(audio_source_done(&s) && k == 640, "bad ids are silent (%zu)", k);
}

/* ---------------------------------------------------------------- demo wav */
static void put32(FILE* f, uint32_t v) { fputc((int)(v & 0xFF), f); fputc((int)((v >> 8) & 0xFF), f); fputc((int)((v >> 16) & 0xFF), f); fputc((int)(v >> 24), f); }
static void put16(FILE* f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }

static void write_demo(const char* path)
{
    static int16_t pcm[16000 * 90];
    size_t n = 0, i;
    audio_seq_t q;
    audio_source_t s;
    int t;
    FILE* f;

#define PLAY() do { audio_source_start(&s, &q, 16000, 40); n += audio_source_read(&s, &pcm[n], sizeof(pcm) / 2 - n); \
                    memset(&pcm[n], 0, 8000 * 2); n += 8000; } while (0)

    for (t = 0; t < AUDIO_TONE_COUNT; t++) { audio_seq_clear(&q); (void)audio_seq_add(&q, AUDIO_SEG_TONE, (uint16_t)t); PLAY(); }
    for (t = 0; t <= AUDIO_CLIP_COMPASS_CAL_DONE; t++) { audio_seq_clear(&q); (void)audio_seq_add(&q, AUDIO_SEG_CLIP, (uint16_t)t); PLAY(); }
    audio_seq_clear(&q); (void)audio_seq_speed_kmh(&q, 32); PLAY();
    audio_seq_clear(&q); (void)audio_seq_altitude_m(&q, 2350); PLAY();
    audio_seq_clear(&q); (void)audio_seq_battery_pct(&q, 85); PLAY();
    audio_seq_clear(&q); (void)audio_seq_heading_deg(&q, 45); PLAY();

    f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite("RIFF", 1, 4, f); put32(f, (uint32_t)(36 + n * 2)); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, 16000); put32(f, 32000); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, (uint32_t)(n * 2));
    for (i = 0; i < n; i++) { put16(f, (uint16_t)pcm[i]); }
    fclose(f);
    printf("  wrote %s (%.1f s)\n", path, (double)n / 16000.0);
}

int main(int argc, char** argv)
{
    test_clock();
    test_driver();
    test_synth();
    test_adpcm();
    test_words();
    test_source();
    if (argc > 1) { write_demo(argv[1]); }
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
