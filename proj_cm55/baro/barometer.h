/*******************************************************************************
* File Name        : barometer.h
*
* Description      : XENSIV(TM) DPS368 barometric pressure sensor wrapper.
*                    Reads pressure / temperature over I2C and converts the
*                    pressure to an altitude for the HUD ski goggle prototype.
*
*******************************************************************************/
#ifndef BAROMETER_H
#define BAROMETER_H

#include <stdbool.h>
#include "cy_pdl.h"
#include "cy_result.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*******************************************************************************
* Macros
*******************************************************************************/
/* Reference sea level pressure (QNH) in hPa used for the absolute altitude.
 * ISA standard atmosphere by default. Can later be corrected with GNSS altitude
 * received from the LoRa Tracker or a known resort elevation. */
#ifndef BARO_SEA_LEVEL_HPA
#define BARO_SEA_LEVEL_HPA          (1013.25f)
#endif

/* Exponential moving average factor for the pressure filter (0..1).
 * Smaller = smoother but slower response. */
#ifndef BARO_FILTER_ALPHA
#define BARO_FILTER_ALPHA           (0.2f)
#endif

/*******************************************************************************
* Data types
*******************************************************************************/
typedef struct
{
    float pressure_hpa;         /* Filtered pressure, hPa */
    float temperature_c;        /* Sensor temperature, degC */
    float altitude_m;           /* Absolute altitude vs. BARO_SEA_LEVEL_HPA, m */
    float rel_altitude_m;       /* Altitude relative to last baro_zero_relative(), m */
} baro_data_t;

/*******************************************************************************
* Function prototypes
*******************************************************************************/
cy_rslt_t baro_init(CySCB_Type* i2c_base, cy_stc_scb_i2c_context_t* i2c_context);
cy_rslt_t baro_update(baro_data_t* data);
void baro_zero_relative(void);
void baro_set_sea_level(float qnh_hpa);
float baro_pressure_to_altitude(float pressure_hpa, float sea_level_hpa);
bool baro_is_ready(void);

#if defined(__cplusplus)
}
#endif

#endif /* BAROMETER_H */

/* [] END OF FILE */
