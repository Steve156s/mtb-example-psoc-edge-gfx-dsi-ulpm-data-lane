/*******************************************************************************
* File Name        : radar_dsp.c
*
* Description      : See radar_dsp.h. Plain C99 + libm, no CMSIS-DSP needed
*                    (the whole frame is ~100 64-point FFTs, well below 1 ms
*                    on the CM55 at 400 MHz).
*
*******************************************************************************/
#include <math.h>
#include <string.h>

#include "radar_config.h"
#include "radar_dsp.h"

#define PI_F            (3.14159265358979f)
#define RAD_TO_DEG      (57.2957795f)
#define FFT_LOG2N       (6U)

#if ((1U << FFT_LOG2N) != RADAR_DSP_SAMPLES)
#error "FFT size and RADAR_DSP_SAMPLES do not match"
#endif

/* Range spectra of the current frame (24 KB) */
static float spec_re[RADAR_DSP_RX][RADAR_DSP_CHIRPS][RADAR_DSP_BINS];
static float spec_im[RADAR_DSP_RX][RADAR_DSP_CHIRPS][RADAR_DSP_BINS];

static float window[RADAR_DSP_SAMPLES];
static float tw_re[RADAR_DSP_SAMPLES / 2U];
static float tw_im[RADAR_DSP_SAMPLES / 2U];
static uint8_t bitrev[RADAR_DSP_SAMPLES];
static bool tables_ready = false;


static void build_tables(void)
{
    for (uint32_t n = 0U; n < RADAR_DSP_SAMPLES; n++)
    {
        uint32_t r = 0U;

        window[n] = 0.5f - (0.5f * cosf((2.0f * PI_F * (float)n) / (float)(RADAR_DSP_SAMPLES - 1U)));
        for (uint32_t b = 0U; b < FFT_LOG2N; b++)
        {
            if (0U != (n & (1U << b)))
            {
                r |= 1U << (FFT_LOG2N - 1U - b);
            }
        }
        bitrev[n] = (uint8_t)r;
    }
    for (uint32_t k = 0U; k < (RADAR_DSP_SAMPLES / 2U); k++)
    {
        float a = (-2.0f * PI_F * (float)k) / (float)RADAR_DSP_SAMPLES;
        tw_re[k] = cosf(a);
        tw_im[k] = sinf(a);
    }
    tables_ready = true;
}


/* In-place radix-2 DIT FFT of RADAR_DSP_SAMPLES points (input already in
 * bit-reversed order). */
static void fft64(float* re, float* im)
{
    for (uint32_t len = 2U; len <= RADAR_DSP_SAMPLES; len <<= 1U)
    {
        uint32_t half = len >> 1U;
        uint32_t step = RADAR_DSP_SAMPLES / len;

        for (uint32_t i = 0U; i < RADAR_DSP_SAMPLES; i += len)
        {
            for (uint32_t j = 0U; j < half; j++)
            {
                float wr = tw_re[j * step];
                float wi = tw_im[j * step];
                uint32_t p = i + j;
                uint32_t q = p + half;
                float xr = (re[q] * wr) - (im[q] * wi);
                float xi = (re[q] * wi) + (im[q] * wr);

                re[q] = re[p] - xr;
                im[q] = im[p] - xi;
                re[p] += xr;
                im[p] += xi;
            }
        }
    }
}


/* Phase difference -> angle for lambda/2 antenna spacing */
static float phase_to_deg(float dphi)
{
    float s = dphi / PI_F;

    if (s > 1.0f)
    {
        s = 1.0f;
    }
    if (s < -1.0f)
    {
        s = -1.0f;
    }
    return asinf(s) * RAD_TO_DEG;
}


void radar_dsp_init(radar_dsp_t* dsp)
{
    if (!tables_ready)
    {
        build_tables();
    }
    (void)memset(dsp, 0, sizeof(*dsp));
}


void radar_dsp_process(radar_dsp_t* dsp, const uint16_t* raw,
                       radar_clutter_mode_t mode, radar_frame_result_t* out)
{
    float re[RADAR_DSP_SAMPLES];
    float im[RADAR_DSP_SAMPLES];
    float energy[RADAR_DSP_BINS];
    float mean_re[RADAR_DSP_RX][RADAR_DSP_BINS];
    float mean_im[RADAR_DSP_RX][RADAR_DSP_BINS];
    uint32_t kmin;
    uint32_t kmax;
    uint32_t kpk;
    float alpha;

    if (!tables_ready)
    {
        build_tables();
    }

    (void)memset(out, 0, sizeof(*out));

    /* 1. Range FFT per antenna and chirp */
    for (uint32_t a = 0U; a < RADAR_DSP_RX; a++)
    {
        for (uint32_t c = 0U; c < RADAR_DSP_CHIRPS; c++)
        {
            const uint16_t* p = &raw[(c * RADAR_DSP_SAMPLES * RADAR_DSP_RX) + a];
            float mean = 0.0f;

            for (uint32_t s = 0U; s < RADAR_DSP_SAMPLES; s++)
            {
                mean += (float)p[s * RADAR_DSP_RX];
            }
            mean /= (float)RADAR_DSP_SAMPLES;

            for (uint32_t s = 0U; s < RADAR_DSP_SAMPLES; s++)
            {
                uint32_t d = bitrev[s];
                re[d] = ((float)p[s * RADAR_DSP_RX] - mean) * window[s];
                im[d] = 0.0f;
            }
            fft64(re, im);
            for (uint32_t k = 0U; k < RADAR_DSP_BINS; k++)
            {
                spec_re[a][c][k] = re[k];
                spec_im[a][c][k] = im[k];
            }
        }
    }

    /* 2. Subtract the static clutter map learned so far */
    for (uint32_t a = 0U; a < RADAR_DSP_RX; a++)
    {
        for (uint32_t k = 0U; k < RADAR_DSP_BINS; k++)
        {
            float mr = 0.0f;
            float mi = 0.0f;

            for (uint32_t c = 0U; c < RADAR_DSP_CHIRPS; c++)
            {
                mr += spec_re[a][c][k];
                mi += spec_im[a][c][k];
            }
            mean_re[a][k] = mr / (float)RADAR_DSP_CHIRPS;
            mean_im[a][k] = mi / (float)RADAR_DSP_CHIRPS;

            if (!dsp->initialized)
            {
                dsp->clutter_re[a][k] = mean_re[a][k];
                dsp->clutter_im[a][k] = mean_im[a][k];
            }
            for (uint32_t c = 0U; c < RADAR_DSP_CHIRPS; c++)
            {
                spec_re[a][c][k] -= dsp->clutter_re[a][k];
                spec_im[a][c][k] -= dsp->clutter_im[a][k];
            }
        }
    }

    /* 3. Energy per range bin, peak inside the detection window */
    for (uint32_t k = 0U; k < RADAR_DSP_BINS; k++)
    {
        float e = 0.0f;

        for (uint32_t a = 0U; a < RADAR_DSP_RX; a++)
        {
            for (uint32_t c = 0U; c < RADAR_DSP_CHIRPS; c++)
            {
                e += (spec_re[a][c][k] * spec_re[a][c][k]) +
                     (spec_im[a][c][k] * spec_im[a][c][k]);
            }
        }
        energy[k] = e;
    }

    kmin = (uint32_t)(RADAR_RANGE_MIN_CM / RADAR_DSP_BIN_CM);
    kmax = (uint32_t)(RADAR_RANGE_MAX_CM / RADAR_DSP_BIN_CM);
    if (kmin < 1U)
    {
        kmin = 1U;
    }
    if (kmax > (RADAR_DSP_BINS - 2U))
    {
        kmax = RADAR_DSP_BINS - 2U;
    }

    kpk = kmin;
    for (uint32_t k = kmin; k <= kmax; k++)
    {
        if (energy[k] > energy[kpk])
        {
            kpk = k;
        }
    }

    if (!dsp->initialized)
    {
        /* First frame only builds the clutter map */
        dsp->noise = RADAR_MIN_ENERGY / RADAR_SNR_FACTOR;
        dsp->initialized = true;
        dsp->frames = 1U;
        out->noise = dsp->noise;
        return;
    }
    dsp->frames++;

    out->peak_bin = kpk;
    out->energy = energy[kpk];
    out->present = (energy[kpk] > (dsp->noise * RADAR_SNR_FACTOR)) &&
                   (energy[kpk] > RADAR_MIN_ENERGY);

    if (!out->present)
    {
        /* adapt the noise floor only while nothing is detected */
        dsp->noise += RADAR_NOISE_ALPHA * (energy[kpk] - dsp->noise);
    }
    out->noise = dsp->noise;

    /* Update the clutter map after the decision, so that a hand that just
     * appeared is not learned into the background (it would remain as a
     * ghost target after it leaves). */
    switch (mode)
    {
        case RADAR_CLUTTER_FREEZE:
            alpha = RADAR_CLUTTER_ALPHA_ACTIVE;
            break;
        case RADAR_CLUTTER_ABSORB:
            alpha = RADAR_CLUTTER_ALPHA_IDLE;
            break;
        case RADAR_CLUTTER_LEARN:
        default:
            alpha = out->present ? RADAR_CLUTTER_ALPHA_ACTIVE : RADAR_CLUTTER_ALPHA_IDLE;
            break;
    }
    if (alpha > 0.0f)
    {
        for (uint32_t a = 0U; a < RADAR_DSP_RX; a++)
        {
            for (uint32_t k = 0U; k < RADAR_DSP_BINS; k++)
            {
                dsp->clutter_re[a][k] += alpha * (mean_re[a][k] - dsp->clutter_re[a][k]);
                dsp->clutter_im[a][k] += alpha * (mean_im[a][k] - dsp->clutter_im[a][k]);
            }
        }
    }

    /* Sub-bin range by parabolic interpolation of the energy peak */
    {
        float y0 = energy[kpk - 1U];
        float y1 = energy[kpk];
        float y2 = energy[kpk + 1U];
        float den = y0 - (2.0f * y1) + y2;
        float delta = 0.0f;

        if (den < 0.0f)
        {
            delta = 0.5f * (y0 - y2) / den;
            if (delta > 0.5f)
            {
                delta = 0.5f;
            }
            if (delta < -0.5f)
            {
                delta = -0.5f;
            }
        }
        out->range_cm = ((float)kpk + delta) * RADAR_DSP_BIN_CM;
    }

    /* 4. Angles and radial velocity at the peak bin */
    {
        float az_r = 0.0f, az_i = 0.0f;
        float el_r = 0.0f, el_i = 0.0f;
        float dp_r = 0.0f, dp_i = 0.0f;
        const float lambda = RADAR_DSP_LIGHT_SPEED / RADAR_DSP_CENTER_HZ;

        for (uint32_t c = 0U; c < RADAR_DSP_CHIRPS; c++)
        {
            float ar = spec_re[RADAR_RX_AZ_A][c][kpk], ai = spec_im[RADAR_RX_AZ_A][c][kpk];
            float br = spec_re[RADAR_RX_AZ_B][c][kpk], bi = spec_im[RADAR_RX_AZ_B][c][kpk];
            float er = spec_re[RADAR_RX_EL_A][c][kpk], ei = spec_im[RADAR_RX_EL_A][c][kpk];
            float fr = spec_re[RADAR_RX_EL_B][c][kpk], fi = spec_im[RADAR_RX_EL_B][c][kpk];

            /* A * conj(B) */
            az_r += (ar * br) + (ai * bi);
            az_i += (ai * br) - (ar * bi);
            el_r += (er * fr) + (ei * fi);
            el_i += (ei * fr) - (er * fi);

            if (c > 0U)
            {
                /* sum over antennas of X[c] * conj(X[c-1]) */
                for (uint32_t a = 0U; a < RADAR_DSP_RX; a++)
                {
                    float xr = spec_re[a][c][kpk], xi = spec_im[a][c][kpk];
                    float pr = spec_re[a][c - 1U][kpk], pi = spec_im[a][c - 1U][kpk];

                    dp_r += (xr * pr) + (xi * pi);
                    dp_i += (xi * pr) - (xr * pi);
                }
            }
        }

        out->azimuth_deg   = (float)RADAR_AZ_SIGN * phase_to_deg(atan2f(az_i, az_r));
        out->elevation_deg = (float)RADAR_EL_SIGN * phase_to_deg(atan2f(el_i, el_r));
        out->velocity_mps  = (atan2f(dp_i, dp_r) * lambda) /
                             (4.0f * PI_F * RADAR_DSP_CHIRP_TIME_S);
    }
}

/* [] END OF FILE */
