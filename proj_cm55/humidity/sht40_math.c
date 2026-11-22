/*******************************************************************************
* File Name        : sht40_math.c
*
* Description      : See sht40_math.h.
*
*******************************************************************************/
#include <math.h>
#include <stddef.h>

#include "sht40_math.h"

/* Magnus coefficients (Sensirion "Dew-point Calculation" application note) */
#define MAGNUS_WATER_B      (17.62f)
#define MAGNUS_WATER_C      (243.12f)
#define MAGNUS_ICE_B        (22.46f)
#define MAGNUS_ICE_C        (272.62f)

#define RH_MIN_FOR_LOG      (0.1f)


float sht40_clamp_rh(float rh_pct)
{
    if (rh_pct < 0.0f)
    {
        return 0.0f;
    }
    if (rh_pct > 100.0f)
    {
        return 100.0f;
    }
    return rh_pct;
}


static float magnus(float t_c, float rh_pct, float b, float c)
{
    float rh = sht40_clamp_rh(rh_pct);
    float gamma;

    if (rh < RH_MIN_FOR_LOG)
    {
        rh = RH_MIN_FOR_LOG;
    }

    gamma = logf(rh / 100.0f) + ((b * t_c) / (c + t_c));
    return (c * gamma) / (b - gamma);
}


float sht40_dew_point_c(float t_c, float rh_pct)
{
    return magnus(t_c, rh_pct, MAGNUS_WATER_B, MAGNUS_WATER_C);
}


float sht40_frost_point_c(float t_c, float rh_pct)
{
    float td;
    float rh;
    float e;        /* actual vapour pressure, hPa */
    float x;

    if (t_c >= 0.0f)
    {
        return sht40_dew_point_c(t_c, rh_pct);
    }

    /* RH from the sensor is relative to liquid water. Convert to the vapour
     * pressure, then invert the Magnus equation over ice. */
    rh = sht40_clamp_rh(rh_pct);
    if (rh < RH_MIN_FOR_LOG)
    {
        rh = RH_MIN_FOR_LOG;
    }
    e = (rh / 100.0f) * 6.112f * expf((MAGNUS_WATER_B * t_c) / (MAGNUS_WATER_C + t_c));
    x = logf(e / 6.112f);
    td = (MAGNUS_ICE_C * x) / (MAGNUS_ICE_B - x);

    return td;
}


float sht40_abs_humidity_gm3(float t_c, float rh_pct)
{
    float rh = sht40_clamp_rh(rh_pct);
    float es = 6.112f * expf((MAGNUS_WATER_B * t_c) / (MAGNUS_WATER_C + t_c));  /* hPa */

    /* rho_v = e / (Rv * T), e in Pa, Rv = 461.5 J/(kg K) -> g/m^3 */
    return (216.7f * (rh / 100.0f) * es) / (273.15f + t_c);
}


sht40_fog_level_t sht40_fog_update(sht40_fog_level_t prev, float margin_c,
                                   float risk_on_c, float fog_on_c,
                                   float hyst_c)
{
    sht40_fog_level_t next;

    /* Level that the margin would give without hysteresis */
    if (margin_c <= fog_on_c)
    {
        next = SHT40_FOG_LIKELY;
    }
    else if (margin_c <= risk_on_c)
    {
        next = SHT40_FOG_RISK;
    }
    else
    {
        next = SHT40_FOG_OK;
    }

    /* Going down a level (improving) requires the margin to clear the
     * threshold by hyst_c. */
    if (next < prev)
    {
        if ((SHT40_FOG_LIKELY == prev) && (margin_c <= (fog_on_c + hyst_c)))
        {
            next = SHT40_FOG_LIKELY;
        }
        else if ((prev >= SHT40_FOG_RISK) && (margin_c <= (risk_on_c + hyst_c)))
        {
            next = SHT40_FOG_RISK;
        }
    }

    return next;
}


bool sht40_batt_heater_update(bool prev_on, float batt_t_c, float on_c, float off_c)
{
    if (batt_t_c <= on_c)
    {
        return true;
    }
    if (batt_t_c >= off_c)
    {
        return false;
    }
    return prev_on;
}


uint32_t sht40_format_fixed(char* buf, uint32_t buf_len, float value,
                            uint32_t decimals, bool force_sign)
{
    static const uint32_t pow10[] = { 1U, 10U, 100U, 1000U };
    char tmp[16];
    uint32_t n = 0U;
    uint32_t len = 0U;
    bool negative = (value < 0.0f);
    float mag;
    uint32_t scaled;
    uint32_t ipart;
    uint32_t fpart;

    if ((NULL == buf) || (0U == buf_len))
    {
        return 0U;
    }
    if (decimals > 3U)
    {
        decimals = 3U;
    }

    mag = negative ? -value : value;
    if (mag > 999999.0f)
    {
        mag = 999999.0f;
    }
    scaled = (uint32_t)((mag * (float)pow10[decimals]) + 0.5f);
    ipart  = scaled / pow10[decimals];
    fpart  = scaled % pow10[decimals];

    if (0U == scaled)
    {
        negative = false;   /* no "-0.0" */
    }

    for (uint32_t d = 0U; d < decimals; d++)
    {
        tmp[n++] = (char)('0' + (fpart % 10U));
        fpart /= 10U;
    }
    if (decimals > 0U)
    {
        tmp[n++] = '.';
    }
    do
    {
        tmp[n++] = (char)('0' + (ipart % 10U));
        ipart /= 10U;
    } while ((0U != ipart) && (n < (sizeof(tmp) - 2U)));

    if (negative)
    {
        tmp[n++] = '-';
    }
    else if (force_sign)
    {
        tmp[n++] = '+';
    }

    while ((n > 0U) && (len < (buf_len - 1U)))
    {
        buf[len++] = tmp[--n];
    }
    buf[len] = '\0';

    return len;
}

/* [] END OF FILE */
