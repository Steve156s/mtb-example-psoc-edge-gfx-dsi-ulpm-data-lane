/*******************************************************************************
* File Name        : sht40_math.h
*
* Description      : Pure (hardware independent) psychrometric helpers and
*                    decision logic for the SHT40 module: dew / frost point,
*                    absolute humidity, lens fog level and battery heater
*                    thermostat. Unit tested on the host in
*                    tools/sht40_host_test.
*
*******************************************************************************/
#ifndef SHT40_MATH_H
#define SHT40_MATH_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum
{
    SHT40_FOG_OK = 0,       /* lens well above the dew point */
    SHT40_FOG_RISK,         /* lens close to the dew point: warn the wearer */
    SHT40_FOG_LIKELY        /* lens at / below the dew point: fogging */
} sht40_fog_level_t;

/* Clamp a raw RH value to the physical range 0..100 %RH (the SHT4x transfer
 * function can return slightly <0 or >100, see datasheet section 4.6). */
float sht40_clamp_rh(float rh_pct);

/* Dew point over liquid water (Magnus formula, Sensirion application note
 * coefficients 17.62 / 243.12 C, valid -45..60 C). RH is clamped to
 * 0.1..100 %. */
float sht40_dew_point_c(float t_c, float rh_pct);

/* Frost point: dew point over ice below 0 C (Magnus 22.46 / 272.62 C);
 * equal to the dew point above 0 C. This is the temperature at which frost
 * forms on the lens when the air is below freezing. */
float sht40_frost_point_c(float t_c, float rh_pct);

/* Absolute humidity in g/m^3 */
float sht40_abs_humidity_gm3(float t_c, float rh_pct);

/* Lens fog level with hysteresis.
 * margin_c = lens temperature - dew (frost) point.
 * risk_on_c > fog_on_c, hyst_c >= 0. */
sht40_fog_level_t sht40_fog_update(sht40_fog_level_t prev, float margin_c,
                                   float risk_on_c, float fog_on_c,
                                   float hyst_c);

/* Battery warming thermostat: returns the new heater request.
 * on_c < off_c. */
bool sht40_batt_heater_update(bool prev_on, float batt_t_c,
                              float on_c, float off_c);

/* Fixed point formatting without printf float support (newlib-nano).
 * Returns the string length. decimals 0..3. */
uint32_t sht40_format_fixed(char* buf, uint32_t buf_len, float value,
                            uint32_t decimals, bool force_sign);

#if defined(__cplusplus)
}
#endif

#endif /* SHT40_MATH_H */
