/*******************************************************************************
* File Name        : sht40_config.h
*
* Description      : Build-time configuration of the SHT40 temperature /
*                    humidity module. Every value can be overridden from the
*                    Makefile, e.g. DEFINES+=SHT40_BATT_HEAT_ON_C=-5.0f
*
*******************************************************************************/
#ifndef SHT40_CONFIG_H
#define SHT40_CONFIG_H

/* 7-bit I2C address. SHT40-AD1B (on KIT_PSE84_EVAL / KIT_PSE84_AI) = 0x44,
 * SHT40-BD1B = 0x45. */
#ifndef SHT40_I2C_ADDR
#define SHT40_I2C_ADDR                  (0x44U)
#endif

/* Measurement period of the sensor task. One high-precision measurement takes
 * 8.3 ms and ~3 uJ, so 1 s keeps the sensor self-heating negligible. */
#ifndef SHT40_SAMPLE_PERIOD_MS
#define SHT40_SAMPLE_PERIOD_MS          (1000U)
#endif

/* Exponential moving average applied to T and RH (1.0 = no filtering) */
#ifndef SHT40_FILTER_ALPHA
#define SHT40_FILTER_ALPHA              (0.5f)
#endif

/* I2C transfer timeout and bus lock timeout */
#ifndef SHT40_I2C_TIMEOUT_MS
#define SHT40_I2C_TIMEOUT_MS            (20U)
#endif
#ifndef SHT40_I2C_LOCK_TIMEOUT_MS
#define SHT40_I2C_LOCK_TIMEOUT_MS       (100U)
#endif

/* Consecutive failed reads before the state goes to SHT40_STATUS_ERROR */
#ifndef SHT40_MAX_ERRORS
#define SHT40_MAX_ERRORS                (3U)
#endif

/* ---------------------------------------------------------------------------
 * Lens fog warning
 * The lens inner surface fogs when its temperature drops below the dew point
 * of the air inside the goggle. Without a lens temperature sensor the lens is
 * estimated as (air temperature - SHT40_LENS_DELTA_C); override the weak hook
 * sht40_port_get_lens_temp_c() when an NTC on the lens is available.
 * margin = lens temperature - dew point
 *   margin <= SHT40_FOG_ON_C                      -> FOG
 *   margin <= SHT40_FOG_RISK_ON_C                 -> RISK
 *   otherwise                                     -> OK
 * SHT40_FOG_HYST_C is added when leaving a level to avoid flicker.
 * ------------------------------------------------------------------------- */
#ifndef SHT40_LENS_DELTA_C
#define SHT40_LENS_DELTA_C              (3.0f)
#endif
#ifndef SHT40_FOG_RISK_ON_C
#define SHT40_FOG_RISK_ON_C             (2.0f)
#endif
#ifndef SHT40_FOG_ON_C
#define SHT40_FOG_ON_C                  (0.0f)
#endif
#ifndef SHT40_FOG_HYST_C
#define SHT40_FOG_HYST_C                (0.5f)
#endif

/* ---------------------------------------------------------------------------
 * Battery warming heater request (hysteresis thermostat)
 * Uses the battery temperature from the weak hook
 * sht40_port_get_battery_temp_c() if provided, otherwise the SHT40 air
 * temperature. Li-ion cells lose capacity below 0 C and must not be charged
 * below 0 C, so the heater is requested at or below SHT40_BATT_HEAT_ON_C and
 * released at or above SHT40_BATT_HEAT_OFF_C.
 * ------------------------------------------------------------------------- */
#ifndef SHT40_BATT_HEAT_ON_C
#define SHT40_BATT_HEAT_ON_C            (0.0f)
#endif
#ifndef SHT40_BATT_HEAT_OFF_C
#define SHT40_BATT_HEAT_OFF_C           (5.0f)
#endif

/* ---------------------------------------------------------------------------
 * SHT40 on-chip heater (condensation / creep recovery)
 * After SHT40_CREEP_RH_PCT or more for SHT40_CREEP_TIME_S seconds the sensor
 * may be wet; a 0.1 s / 110 mW heater pulse dries it. Pulses are at least
 * SHT40_HEATER_MIN_INTERVAL_S apart (datasheet: duty cycle <= 10 %) and the
 * readings of the following SHT40_HEATER_SETTLE_S seconds are discarded.
 * Set SHT40_AUTO_HEATER to 0 to disable.
 * ------------------------------------------------------------------------- */
#ifndef SHT40_AUTO_HEATER
#define SHT40_AUTO_HEATER               (1)
#endif
#ifndef SHT40_CREEP_RH_PCT
#define SHT40_CREEP_RH_PCT              (95.0f)
#endif
#ifndef SHT40_CREEP_TIME_S
#define SHT40_CREEP_TIME_S              (60U)
#endif
#ifndef SHT40_HEATER_MIN_INTERVAL_S
#define SHT40_HEATER_MIN_INTERVAL_S     (60U)
#endif
#ifndef SHT40_HEATER_SETTLE_S
#define SHT40_HEATER_SETTLE_S           (10U)
#endif

#endif /* SHT40_CONFIG_H */
