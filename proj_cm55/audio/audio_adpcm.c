/*******************************************************************************
* File Name        : audio_adpcm.c
*
* Description      : IMA ADPCM decoder / encoder, see header.
*
*******************************************************************************/
#include "audio_adpcm.h"

static const int16_t step_table[89] =
{
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
    29794, 32767
};

static const int8_t index_table[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };


static int16_t decode_nibble(int32_t* pred, int32_t* idx, uint8_t code)
{
    int32_t step = step_table[*idx];
    int32_t dq = step >> 3;

    if (0U != (code & 4U)) { dq += step; }
    if (0U != (code & 2U)) { dq += step >> 1; }
    if (0U != (code & 1U)) { dq += step >> 2; }

    *pred += (0U != (code & 8U)) ? -dq : dq;
    if (*pred > 32767)  { *pred = 32767; }
    if (*pred < -32768) { *pred = -32768; }

    *idx += index_table[code & 7U];
    if (*idx < 0)  { *idx = 0; }
    if (*idx > 88) { *idx = 88; }

    return (int16_t)*pred;
}


void audio_adpcm_start(audio_adpcm_t* d, const uint8_t* data, uint32_t samples)
{
    d->data = data;
    d->total = samples;
    d->pos = 0U;
    d->predictor = 0;
    d->index = 0;
}


size_t audio_adpcm_decode(audio_adpcm_t* d, int16_t* out, size_t n)
{
    size_t k = 0U;

    while ((k < n) && (d->pos < d->total))
    {
        uint8_t b = d->data[d->pos >> 1];
        uint8_t code = (0U == (d->pos & 1U)) ? (uint8_t)(b & 0x0FU) : (uint8_t)(b >> 4);

        out[k++] = decode_nibble(&d->predictor, &d->index, code);
        d->pos++;
    }

    return k;
}


void audio_adpcm_encode(const int16_t* in, size_t n, uint8_t* out)
{
    int32_t pred = 0, idx = 0;
    size_t i;

    for (i = 0U; i < n; i++)
    {
        int32_t step = step_table[idx];
        int32_t diff = (int32_t)in[i] - pred;
        uint8_t code = 0U;

        if (diff < 0) { code = 8U; diff = -diff; }
        if (diff >= step) { code |= 4U; diff -= step; }
        step >>= 1;
        if (diff >= step) { code |= 2U; diff -= step; }
        step >>= 1;
        if (diff >= step) { code |= 1U; }

        /* Track the decoder exactly */
        (void)decode_nibble(&pred, &idx, code);

        if (0U == (i & 1U))
        {
            out[i >> 1] = code;
        }
        else
        {
            out[i >> 1] |= (uint8_t)(code << 4);
        }
    }
}

/* [] END OF FILE */
