/*******************************************************************************
* File Name        : sht40_env.h
*
* Description      : SHT40 temperature / humidity module for the HUD goggle
*                    prototype. A FreeRTOS task measures T / RH once per
*                    second through the Sensirion embedded-i2c-sht4x driver,
*                    computes dew / frost point and derives:
*                      - lens fog warning level (OK / RISK / FOG)
*                      - battery warming heater request (ON / OFF)
*                    Other tasks read a consistent snapshot with
*                    sht40_env_get_state().
*
*******************************************************************************/
#ifndef SHT40_ENV_H
#define SHT40_ENV_H

#include <stdbool.h>
#include <stdint.h>

#include "cy_pdl.h"
#include "cy_result.h"
#include "FreeRTOS.h"

#include "sht40_math.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SHT40_ENV_RSLT_ERR      (CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_BASE, 0x441U))

typedef enum
{
    SHT40_STATUS_INIT = 0,      /* task started, no measurement yet */
    SHT40_STATUS_OK,            /* valid data */
    SHT40_STATUS_HEATING,       /* on-chip heater pulse, last data held */
    SHT40_STATUS_NO_SENSOR,     /* no ACK from SHT40_I2C_ADDR at start-up */
    SHT40_STATUS_ERROR          /* repeated read / CRC errors */
} sht40_status_t;

typedef struct
{
    sht40_status_t    status;
    uint32_t          serial_number;
    float             temperature_c;    /* filtered air temperature */
    float             humidity_pct;     /* filtered RH, 0..100 */
    float             dew_point_c;      /* over water */
    float             frost_point_c;    /* over ice below 0 C, else = dew point */
    float             abs_humidity_gm3;
    float             lens_temp_c;      /* measured (hook) or estimated */
    float             fog_margin_c;     /* lens temperature - condensation point */
    sht40_fog_level_t fog_level;
    bool              batt_heater_on;   /* battery warming request */
    float             batt_temp_c;      /* temperature used by the thermostat */
    uint32_t          sample_count;
    uint32_t          error_count;
} sht40_state_t;

/* Binds the I2C controller (already initialized and enabled by the caller,
 * e.g. the display I2C in main.c) and starts the sensor task. Returns an
 * error only if the task could not be created; a missing sensor is reported
 * through sht40_state_t.status. */
cy_rslt_t sht40_env_start(CySCB_Type* base, cy_stc_scb_i2c_context_t* context,
                          UBaseType_t priority);

/* Thread-safe copy of the latest state */
void sht40_env_get_state(sht40_state_t* state);

/* Short labels for the HUD / log */
const char* sht40_fog_level_str(sht40_fog_level_t level);
const char* sht40_status_str(sht40_status_t status);

/*******************************************************************************
* Porting hooks (weak defaults in sht40_env.c, override in the product code)
*******************************************************************************/

/* Lens inner surface temperature (e.g. NTC on the lens). Return false when
 * not available: the module then estimates it as T - SHT40_LENS_DELTA_C. */
bool sht40_port_get_lens_temp_c(float* lens_temp_c);

/* Battery pack temperature (e.g. fuel gauge / NTC). Return false when not
 * available: the module then uses the SHT40 air temperature. */
bool sht40_port_get_battery_temp_c(float* batt_temp_c);

/* Called from the sensor task when the battery heater request changes.
 * Default: no-op (prototype has no heater). Drive the heater MOSFET here. */
void sht40_port_battery_heater(bool on);

/* Called from the sensor task when the fog level changes. Default: no-op.
 * Hook for a lens heater / fan or a voice prompt. */
void sht40_port_fog_level_changed(sht40_fog_level_t level);

#if defined(__cplusplus)
}
#endif

#endif /* SHT40_ENV_H */
