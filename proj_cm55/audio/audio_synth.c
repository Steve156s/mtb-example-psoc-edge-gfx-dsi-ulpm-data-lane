/*******************************************************************************
* File Name        : audio_synth.c
*
* Description      : Prompt tone synthesizer, see header.
*
*******************************************************************************/
#include <stddef.h>

#include "audio_synth.h"

#define ATTACK_MS       (4U)
#define RELEASE_MS      (8U)

/* 32767 * sin(2 pi i / 256), i = 0..256 (last entry = guard for interpolation) */
static const int16_t sine_q15[257] =
{
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,   6393,   7179,   7962,   8739,
      9512,  10278,  11039,  11793,  12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,  23170,  23731,  24279,  24811,
     25329,  25832,  26319,  26790,  27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,  32137,  32285,  32412,  32521,
     32609,  32678,  32728,  32757,  32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,  30273,  29956,  29621,  29268,
     28898,  28510,  28105,  27683,  27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,  18204,  17530,  16846,  16151,
     15446,  14732,  14010,  13279,  12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,      0,   -804,  -1608,  -2410,
     -3212,  -4011,  -4808,  -5602,  -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
    -20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956, -30273, -30571, -30852, -31113,
    -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580,
    -31356, -31113, -30852, -30571, -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
    -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,  -6393,  -5602,  -4808,  -4011,
     -3212,  -2410,  -1608,   -804,      0,
};

/* Amplitudes: about -6 dBFS for normal prompts, -3 dBFS for alarms */
#define A_NORM          (16384U)
#define A_LOUD          (23170U)
#define A_SOFT          (8192U)

static const audio_note_t tone_boot[] =
{
    {  523,  523,  90, A_NORM }, {  659,  659,  90, A_NORM },
    {  784,  784,  90, A_NORM }, { 1047, 1047, 220, A_NORM },
};
static const audio_note_t tone_click[] =
{
    { 2000, 2000,  15, A_SOFT },
};
static const audio_note_t tone_confirm[] =
{
    {  880,  880,  60, A_NORM }, {    0,    0,  40, 0 }, { 1320, 1320,  90, A_NORM },
};
static const audio_note_t tone_warning[] =
{
    {  880,  880, 150, A_LOUD }, {  660,  660, 150, A_LOUD },
    {  880,  880, 150, A_LOUD }, {  660,  660, 150, A_LOUD },
};
static const audio_note_t tone_low_battery[] =
{
    {  784,  784, 200, A_NORM }, {    0,    0,  40, 0 }, {  523,  523, 300, A_NORM },
};
static const audio_note_t tone_ptt_start[] =
{
    {  600, 1200,  80, A_NORM },
};
static const audio_note_t tone_ptt_end[] =
{
    { 1200,  600,  80, A_NORM },
};
static const audio_note_t tone_message[] =
{
    { 1047, 1047,  70, A_NORM }, {    0,    0,  50, 0 }, { 1047, 1047,  70, A_NORM },
};
static const audio_note_t tone_sos[] =
{
    { 1800, 1800, 100, A_LOUD }, { 0, 0, 100, 0 }, { 1800, 1800, 100, A_LOUD }, { 0, 0, 100, 0 },
    { 1800, 1800, 100, A_LOUD }, { 0, 0, 300, 0 },
    { 1800, 1800, 300, A_LOUD }, { 0, 0, 100, 0 }, { 1800, 1800, 300, A_LOUD }, { 0, 0, 100, 0 },
    { 1800, 1800, 300, A_LOUD }, { 0, 0, 300, 0 },
    { 1800, 1800, 100, A_LOUD }, { 0, 0, 100, 0 }, { 1800, 1800, 100, A_LOUD }, { 0, 0, 100, 0 },
    { 1800, 1800, 100, A_LOUD },
};
static const audio_note_t tone_error[] =
{
    {  330,  330, 300, A_NORM },
};

#define TONE(t, n)  { (t), (uint8_t)(sizeof(t) / sizeof((t)[0])), (n) }

const audio_tone_def_t audio_tones[AUDIO_TONE_COUNT] =
{
    TONE(tone_boot,        "BOOT"),
    TONE(tone_click,       "CLICK"),
    TONE(tone_confirm,     "CONFIRM"),
    TONE(tone_warning,     "WARNING"),
    TONE(tone_low_battery, "LOW_BATTERY"),
    TONE(tone_ptt_start,   "PTT_START"),
    TONE(tone_ptt_end,     "PTT_END"),
    TONE(tone_message,     "MESSAGE"),
    TONE(tone_sos,         "SOS"),
    TONE(tone_error,       "ERROR"),
};


int16_t audio_sin_q15(uint32_t phase)
{
    uint32_t i = phase >> 24;                   /* table index 0..255 */
    int32_t frac = (int32_t)((phase >> 8) & 0xFFFFU);
    int32_t a = sine_q15[i];
    int32_t b = sine_q15[i + 1U];

    return (int16_t)(a + (((b - a) * frac) >> 16));
}


uint32_t audio_phase_inc(uint32_t f_hz, uint32_t fs_hz)
{
    return (uint32_t)((((uint64_t)f_hz) << 32) / fs_hz);
}


static uint32_t note_len(const audio_note_t* n, uint32_t fs_hz)
{
    return (uint32_t)(((uint64_t)n->ms * fs_hz) / 1000U);
}


uint32_t audio_synth_length(const audio_note_t* notes, uint8_t count, uint32_t fs_hz)
{
    uint32_t total = 0U;
    uint8_t i;

    for (i = 0U; i < count; i++)
    {
        total += note_len(&notes[i], fs_hz);
    }
    return total;
}


void audio_synth_start(audio_synth_t* s, const audio_note_t* notes, uint8_t count, uint32_t fs_hz)
{
    s->notes = notes;
    s->count = count;
    s->index = 0U;
    s->pos = 0U;
    s->phase = 0U;
    s->fs_hz = fs_hz;
    s->len = (count > 0U) ? note_len(&notes[0], fs_hz) : 0U;
}


size_t audio_synth_render(audio_synth_t* s, int16_t* out, size_t n)
{
    size_t k = 0U;
    uint32_t att = (ATTACK_MS * s->fs_hz) / 1000U;
    uint32_t rel = (RELEASE_MS * s->fs_hz) / 1000U;

    while ((k < n) && (s->index < s->count))
    {
        const audio_note_t* nt = &s->notes[s->index];

        if (s->pos >= s->len)
        {
            s->index++;
            s->pos = 0U;
            s->phase = 0U;      /* each note starts at a zero crossing */
            s->len = (s->index < s->count) ? note_len(&s->notes[s->index], s->fs_hz) : 0U;
            continue;
        }

        if ((0U == nt->f_start_hz) || (0U == nt->amp))
        {
            out[k++] = 0;
        }
        else
        {
            int64_t f0 = (int64_t)audio_phase_inc(nt->f_start_hz, s->fs_hz);
            int64_t f1 = (int64_t)audio_phase_inc(nt->f_end_hz, s->fs_hz);
            uint32_t inc = (uint32_t)(f0 + (((f1 - f0) * (int64_t)s->pos) / (int64_t)s->len));
            uint32_t env = 32768U;  /* Q15 gain */
            uint32_t tail = s->len - s->pos;

            if ((att > 0U) && (s->pos < att))
            {
                env = (s->pos * 32768U) / att;
            }
            if ((rel > 0U) && (tail <= rel))
            {
                uint32_t r = ((tail - 1U) * 32768U) / rel;
                env = (r < env) ? r : env;
            }

            out[k++] = (int16_t)((((int32_t)audio_sin_q15(s->phase) * (int32_t)nt->amp) >> 15) * (int32_t)env >> 15);
            s->phase += inc;
        }
        s->pos++;
    }

    return k;
}

/* [] END OF FILE */
