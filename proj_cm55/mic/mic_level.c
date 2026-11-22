/*******************************************************************************
* File Name        : mic_level.c
*
* Description      : Portable level meter + sound activity detector.
*                    See mic_level.h.
*
*******************************************************************************/
#include "mic_level.h"

#include <stddef.h>

/*******************************************************************************
* Macros
*******************************************************************************/
/* DC blocker pole: y[n] = x[n] - x[n-1] + a*y[n-1], a = 0.995 (Q15).
 * -3 dB corner ~13 Hz at 16 kHz: removes the IM7x / bias offset, keeps voice. */
#define DC_POLE_Q15                     (32604)

/* Noise floor tracking (Q8 dB*10 per frame) */
#define FLOOR_FALL_SHIFT                (3)     /* follow quieter frames fast (1/8) */
#define FLOOR_RISE_Q8                   (13)    /* rise ~0.05 dB per frame when idle */
#define FLOOR_RISE_ACTIVE_Q8            (2)     /* barely rise during activity */


/*******************************************************************************
* Function Name: log2_q16
********************************************************************************
* Summary:
*  Integer log2 of a 64-bit value in Q16 (x > 0).
*******************************************************************************/
static int32_t log2_q16(uint64_t x)
{
    int32_t int_part = 63;
    uint64_t m = x;
    uint64_t y;
    int32_t frac = 0;

    while (0U == (m & 0x8000000000000000ULL))
    {
        m <<= 1;
        int_part--;
    }

    /* y = mantissa in Q31, 1.0 <= y < 2.0 */
    y = m >> 32;

    for (int32_t bit = 15; bit >= 0; bit--)
    {
        y = (y * y) >> 31;
        if (y >= 0x100000000ULL)
        {
            y >>= 1;
            frac |= (int32_t)1 << bit;
        }
    }

    return (int_part * (int32_t)65536) + frac;
}


/*******************************************************************************
* Function Name: mic_level_ms_to_db10
*******************************************************************************/
int16_t mic_level_ms_to_db10(uint64_t mean_square)
{
    int64_t db10;

    if (0U == mean_square)
    {
        return (int16_t)MIC_LEVEL_FLOOR_DB10;
    }

    /* dBFS*10 = 100*log10(ms / 2^30) = 30.103 * (log2(ms) - 30) */
    db10 = ((int64_t)log2_q16(mean_square) - (30LL * 65536LL)) * 30103LL;
    db10 = (db10 >= 0) ? ((db10 + 32768000LL) / 65536000LL)
                       : ((db10 - 32768000LL) / 65536000LL);

    if (db10 < MIC_LEVEL_FLOOR_DB10)
    {
        db10 = MIC_LEVEL_FLOOR_DB10;
    }
    if (db10 > 60)
    {
        db10 = 60;
    }
    return (int16_t)db10;
}


/*******************************************************************************
* Function Name: mic_level_init
*******************************************************************************/
void mic_level_init(mic_level_t *lv, int16_t vu_min_db10, int16_t release_db10,
                    uint16_t peak_hold_frames, int16_t clip_level)
{
    lv->vu_min_db10      = vu_min_db10;
    lv->release_db10     = release_db10;
    lv->peak_hold_frames = peak_hold_frames;
    lv->clip_level       = clip_level;

    lv->dc_x1      = 0;
    lv->dc_y1_q15  = 0;
    lv->rms_db10   = (int16_t)MIC_LEVEL_FLOOR_DB10;
    lv->peak_db10  = (int16_t)MIC_LEVEL_FLOOR_DB10;
    lv->vu_db10    = (int16_t)MIC_LEVEL_FLOOR_DB10;
    lv->hold_db10  = (int16_t)MIC_LEVEL_FLOOR_DB10;
    lv->hold_count = 0U;
    lv->clip_count = 0U;
    lv->frames     = 0U;
}


/*******************************************************************************
* Function Name: mic_level_process
*******************************************************************************/
void mic_level_process(mic_level_t *lv, int16_t *samples, uint32_t count)
{
    uint64_t sum_sq = 0U;
    uint32_t peak = 0U;
    int32_t x1 = lv->dc_x1;
    int32_t y1 = lv->dc_y1_q15;
    int16_t rms;
    int16_t pk;

    if ((NULL == samples) || (0U == count))
    {
        return;
    }

    for (uint32_t i = 0U; i < count; i++)
    {
        int32_t x = samples[i];
        int32_t y;
        uint32_t a;

        /* Clip detection on the raw sample (before DC removal) */
        a = (uint32_t)((x < 0) ? -x : x);
        if (a >= (uint32_t)lv->clip_level)
        {
            lv->clip_count++;
        }

        /* DC blocker, state kept in Q15 for precision */
        {
            int64_t acc = ((int64_t)(x - x1) * 32768LL) +
                          (((int64_t)y1 * DC_POLE_Q15) / 32768LL);
            if (acc > (32767LL * 32768LL))
            {
                acc = 32767LL * 32768LL;
            }
            if (acc < (-32768LL * 32768LL))
            {
                acc = -32768LL * 32768LL;
            }
            y1 = (int32_t)acc;
        }
        x1 = x;
        y  = y1 / (int32_t)32768;
        samples[i] = (int16_t)y;

        sum_sq += (uint64_t)((int64_t)y * y);
        a = (uint32_t)((y < 0) ? -y : y);
        if (a > peak)
        {
            peak = a;
        }
    }

    lv->dc_x1 = x1;
    lv->dc_y1_q15 = y1;

    rms = mic_level_ms_to_db10(sum_sq / count);
    /* peak of a sine is 3 dB above its RMS: ms of a square of height 'peak' */
    pk  = mic_level_ms_to_db10((uint64_t)peak * peak);

    lv->rms_db10  = rms;
    lv->peak_db10 = pk;

    /* VU: instant attack, linear release */
    if (rms >= lv->vu_db10)
    {
        lv->vu_db10 = rms;
    }
    else
    {
        int32_t v = (int32_t)lv->vu_db10 - lv->release_db10;
        lv->vu_db10 = (int16_t)((v < rms) ? rms : v);
    }

    /* Peak hold on the sample peak */
    if (pk >= lv->hold_db10)
    {
        lv->hold_db10 = pk;
        lv->hold_count = lv->peak_hold_frames;
    }
    else if (lv->hold_count > 0U)
    {
        lv->hold_count--;
    }
    else
    {
        int32_t h = (int32_t)lv->hold_db10 - lv->release_db10;
        lv->hold_db10 = (int16_t)((h < pk) ? pk : h);
    }

    lv->frames++;
}


/*******************************************************************************
* Function Name: mic_level_vu_fill
*******************************************************************************/
uint32_t mic_level_vu_fill(const mic_level_t *lv, int16_t db10, uint32_t scale)
{
    int32_t range = -(int32_t)lv->vu_min_db10;
    int32_t pos = (int32_t)db10 - lv->vu_min_db10;

    if ((range <= 0) || (pos <= 0))
    {
        return 0U;
    }
    if (pos >= range)
    {
        return scale;
    }
    return (uint32_t)(((int64_t)pos * scale) / range);
}


/*******************************************************************************
* Function Name: mic_vad_init
*******************************************************************************/
void mic_vad_init(mic_vad_t *vad, int16_t on_db10, int16_t off_db10,
                  int16_t abs_min_db10, uint16_t on_frames,
                  uint16_t hangover_frames)
{
    vad->on_db10         = on_db10;
    vad->off_db10        = off_db10;
    vad->abs_min_db10    = abs_min_db10;
    vad->on_frames       = (on_frames > 0U) ? on_frames : 1U;
    vad->hangover_frames = hangover_frames;

    vad->floor_db10_q8 = 0;
    vad->floor_valid   = false;
    vad->active        = false;
    vad->on_count      = 0U;
    vad->quiet_count   = 0U;
}


/*******************************************************************************
* Function Name: mic_vad_floor_db10
*******************************************************************************/
int16_t mic_vad_floor_db10(const mic_vad_t *vad)
{
    return (int16_t)(vad->floor_db10_q8 / 256);
}


/*******************************************************************************
* Function Name: mic_vad_process
*******************************************************************************/
mic_vad_event_t mic_vad_process(mic_vad_t *vad, int16_t level_db10)
{
    mic_vad_event_t ev = MIC_VAD_EVENT_NONE;
    int32_t level_q8 = (int32_t)level_db10 * 256;
    int32_t floor_db10;
    bool loud;
    bool still;

    if (!vad->floor_valid)
    {
        vad->floor_db10_q8 = level_q8;
        vad->floor_valid = true;
        return MIC_VAD_EVENT_NONE;
    }

    /* Noise floor: falls quickly to quieter frames, rises slowly otherwise */
    if (level_q8 < vad->floor_db10_q8)
    {
        vad->floor_db10_q8 -= (vad->floor_db10_q8 - level_q8) >> FLOOR_FALL_SHIFT;
    }
    else
    {
        vad->floor_db10_q8 += vad->active ? FLOOR_RISE_ACTIVE_Q8 : FLOOR_RISE_Q8;
        if (vad->floor_db10_q8 > level_q8)
        {
            vad->floor_db10_q8 = level_q8;
        }
    }

    floor_db10 = vad->floor_db10_q8 / 256;
    loud  = (level_db10 >= vad->abs_min_db10) &&
            (((int32_t)level_db10 - floor_db10) >= vad->on_db10);
    still = (level_db10 >= vad->abs_min_db10) &&
            (((int32_t)level_db10 - floor_db10) >= vad->off_db10);

    if (!vad->active)
    {
        vad->on_count = loud ? (uint16_t)(vad->on_count + 1U) : 0U;
        if (vad->on_count >= vad->on_frames)
        {
            vad->active = true;
            vad->on_count = 0U;
            vad->quiet_count = 0U;
            ev = MIC_VAD_EVENT_START;
        }
    }
    else
    {
        vad->quiet_count = still ? 0U : (uint16_t)(vad->quiet_count + 1U);
        if (vad->quiet_count >= vad->hangover_frames)
        {
            vad->active = false;
            vad->quiet_count = 0U;
            ev = MIC_VAD_EVENT_END;
        }
    }

    return ev;
}

/* [] END OF FILE */
