/*******************************************************************************
* File Name        : audio_adpcm.h
*
* Description      : IMA ADPCM (4-bit) decoder for the voice prompt clips.
*                    Stream: low nibble first, starts at predictor 0 / index 0
*                    (format written by tools/audio_clips/wav2c.py).
*
*******************************************************************************/
#ifndef AUDIO_ADPCM_H
#define AUDIO_ADPCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const uint8_t* data;
    uint32_t total;          /* samples in the stream */
    uint32_t pos;            /* next sample */
    int32_t  predictor;
    int32_t  index;
} audio_adpcm_t;

void   audio_adpcm_start(audio_adpcm_t* d, const uint8_t* data, uint32_t samples);
size_t audio_adpcm_decode(audio_adpcm_t* d, int16_t* out, size_t n);

/* Encoder: same algorithm as wav2c.py, used by the host test */
void   audio_adpcm_encode(const int16_t* in, size_t n, uint8_t* out);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_ADPCM_H */
