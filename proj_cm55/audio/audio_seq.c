/*******************************************************************************
* File Name        : audio_seq.c
*
* Description      : Playback sequences, sample source and number-to-words,
*                    see header.
*
*******************************************************************************/
#include <string.h>

#include "audio_seq.h"

void audio_seq_clear(audio_seq_t* q)
{
    q->count = 0U;
}


bool audio_seq_add(audio_seq_t* q, audio_seg_type_t type, uint16_t arg)
{
    if (q->count >= AUDIO_SEQ_MAX)
    {
        return false;
    }

    q->seg[q->count].type = (uint8_t)type;
    q->seg[q->count].arg = arg;
    q->count++;
    return true;
}


static bool add_clips(audio_seq_t* q, const uint16_t* ids, size_t n)
{
    size_t i;

    if ((size_t)(AUDIO_SEQ_MAX - q->count) < n)
    {
        return false;
    }

    for (i = 0U; i < n; i++)
    {
        (void)audio_seq_add(q, AUDIO_SEG_CLIP, ids[i]);
    }
    return true;
}


/* Taiwan usage: 兩千 / 兩百 for a leading 2 in the thousands / hundreds,
 * 十 (not 一十) for 10..19 alone, one 零 for each run of inner zeros. */
size_t audio_number_words(uint32_t n, uint16_t* ids, size_t max)
{
    static const uint16_t unit[4] = { 0U, AUDIO_CLIP_D_10, AUDIO_CLIP_D_100, AUDIO_CLIP_D_1000 };
    uint16_t tmp[8];
    size_t k = 0U;
    int pos;
    bool zero_pending = false;
    bool started = false;
    uint32_t div = 1000U;

    if (n > 9999U)
    {
        return 0U;
    }

    if (0U == n)
    {
        tmp[k++] = AUDIO_CLIP_D_0;
    }

    for (pos = 3; pos >= 0; pos--, div /= 10U)
    {
        uint32_t d = (n / div) % 10U;

        if (0U == d)
        {
            if (started)
            {
                zero_pending = true;
            }
            continue;
        }

        if (zero_pending)
        {
            tmp[k++] = AUDIO_CLIP_D_0;
            zero_pending = false;
        }

        if ((1 == pos) && (1U == d) && !started)
        {
            tmp[k++] = AUDIO_CLIP_D_10;              /* 十X */
        }
        else
        {
            if ((2U == d) && (pos >= 2))
            {
                tmp[k++] = AUDIO_CLIP_D_LIANG;       /* 兩百 / 兩千 */
            }
            else
            {
                tmp[k++] = (uint16_t)(AUDIO_CLIP_D_0 + d);
            }

            if (pos > 0)
            {
                tmp[k++] = unit[pos];
            }
        }
        started = true;
    }

    /* Trailing zeros are silent: 1500 = 一千五百 */
    if (k > max)
    {
        return 0U;
    }

    memcpy(ids, tmp, k * sizeof(uint16_t));
    return k;
}


bool audio_seq_add_number(audio_seq_t* q, uint32_t n)
{
    uint16_t ids[8];
    size_t k = audio_number_words(n, ids, 8U);

    return (k > 0U) && add_clips(q, ids, k);
}


static bool phrase(audio_seq_t* q, uint16_t pre, uint32_t n, int post)
{
    uint8_t keep = q->count;
    bool ok = audio_seq_add(q, AUDIO_SEG_CLIP, pre) && audio_seq_add_number(q, n);

    if (ok && (post >= 0))
    {
        ok = audio_seq_add(q, AUDIO_SEG_CLIP, (uint16_t)post);
    }

    if (!ok)
    {
        q->count = keep;
    }
    return ok;
}


bool audio_seq_speed_kmh(audio_seq_t* q, uint32_t kmh)
{
    return phrase(q, AUDIO_CLIP_W_SPEED, (kmh > 9999U) ? 9999U : kmh, AUDIO_CLIP_W_KM);
}


bool audio_seq_altitude_m(audio_seq_t* q, int32_t m)
{
    /* Ski areas are above sea level; negative values are spoken as 0 */
    uint32_t v = (m < 0) ? 0U : (uint32_t)m;
    return phrase(q, AUDIO_CLIP_W_ALTITUDE, (v > 9999U) ? 9999U : v, AUDIO_CLIP_W_METER);
}


bool audio_seq_battery_pct(audio_seq_t* q, uint32_t pct)
{
    uint8_t keep = q->count;
    bool ok = audio_seq_add(q, AUDIO_SEG_CLIP, AUDIO_CLIP_W_BATTERY) &&
              phrase(q, AUDIO_CLIP_W_PERCENT, (pct > 100U) ? 100U : pct, -1);

    if (!ok)
    {
        q->count = keep;
    }
    return ok;
}


audio_clip_id_t audio_heading_clip(uint32_t deg)
{
    /* 8 sectors of 45 degrees centred on N, NE, E, ... */
    uint32_t sector = (((deg % 360U) * 2U + 45U) / 90U) % 8U;
    return (audio_clip_id_t)(AUDIO_CLIP_DIR_N + sector);
}


bool audio_seq_heading_deg(audio_seq_t* q, uint32_t deg)
{
    uint8_t keep = q->count;
    bool ok = audio_seq_add(q, AUDIO_SEG_CLIP, AUDIO_CLIP_W_HEADING) &&
              audio_seq_add(q, AUDIO_SEG_CLIP, (uint16_t)audio_heading_clip(deg));

    if (!ok)
    {
        q->count = keep;
    }
    return ok;
}


/*******************************************************************************
* Source
*******************************************************************************/
void audio_source_start(audio_source_t* s, const audio_seq_t* q, uint32_t fs_hz, uint32_t word_gap_ms)
{
    memset(s, 0, sizeof(*s));
    s->seq = *q;
    s->fs_hz = fs_hz;
    s->word_gap = (word_gap_ms * fs_hz) / 1000U;
}


bool audio_source_done(const audio_source_t* s)
{
    return !s->seg_active && !s->gap_pending && (s->index >= s->seq.count);
}


static void begin_segment(audio_source_t* s)
{
    const audio_seg_t* g = &s->seq.seg[s->index];

    switch (g->type)
    {
        case AUDIO_SEG_TONE:
            if (g->arg < AUDIO_TONE_COUNT)
            {
                audio_synth_start(&s->synth, audio_tones[g->arg].notes, audio_tones[g->arg].count, s->fs_hz);
            }
            else
            {
                audio_synth_start(&s->synth, NULL, 0U, s->fs_hz);
            }
            break;

        case AUDIO_SEG_CLIP:
            if (g->arg < AUDIO_CLIP_COUNT)
            {
                audio_adpcm_start(&s->adpcm, audio_clips[g->arg].data, audio_clips[g->arg].samples);
            }
            else
            {
                audio_adpcm_start(&s->adpcm, NULL, 0U);
            }
            break;

        case AUDIO_SEG_SILENCE:
        default:
            s->silence_left = ((uint32_t)g->arg * s->fs_hz) / 1000U;
            break;
    }
    s->seg_active = true;
}


size_t audio_source_read(audio_source_t* s, int16_t* out, size_t n)
{
    size_t k = 0U;

    while (k < n)
    {
        size_t got;

        if (s->gap_pending)
        {
            size_t z = (size_t)s->silence_left;
            if (z > (n - k)) { z = n - k; }
            memset(&out[k], 0, z * sizeof(int16_t));
            k += z;
            s->silence_left -= (uint32_t)z;
            if (0U == s->silence_left)
            {
                s->gap_pending = false;
            }
            continue;
        }

        if (!s->seg_active)
        {
            if (s->index >= s->seq.count)
            {
                break;
            }
            begin_segment(s);
        }

        switch (s->seq.seg[s->index].type)
        {
            case AUDIO_SEG_TONE:
                got = audio_synth_render(&s->synth, &out[k], n - k);
                break;

            case AUDIO_SEG_CLIP:
                got = audio_adpcm_decode(&s->adpcm, &out[k], n - k);
                break;

            case AUDIO_SEG_SILENCE:
            default:
                got = (size_t)s->silence_left;
                if (got > (n - k)) { got = n - k; }
                memset(&out[k], 0, got * sizeof(int16_t));
                s->silence_left -= (uint32_t)got;
                break;
        }

        k += got;

        if (k < n)
        {
            /* Segment finished: insert the word gap between two sounding
             * segments (not after the last one, not around silences). */
            bool was_silence = (AUDIO_SEG_SILENCE == s->seq.seg[s->index].type);
            s->seg_active = false;
            s->index++;

            if (!was_silence && (s->index < s->seq.count) &&
                (AUDIO_SEG_SILENCE != s->seq.seg[s->index].type) && (s->word_gap > 0U))
            {
                s->silence_left = s->word_gap;
                s->gap_pending = true;
            }
        }
        else if ((AUDIO_SEG_SILENCE == s->seq.seg[s->index].type) && (0U == s->silence_left))
        {
            s->seg_active = false;
            s->index++;
        }
    }

    return k;
}

/* [] END OF FILE */
