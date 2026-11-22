/*******************************************************************************
* File Name        : barometer.c
*
* Description      : XENSIV(TM) DPS368 barometric pressure sensor wrapper.
*                    Uses the Infineon sensor-xensiv-dps3xx library on the
*                    board I2C controller (CYBSP_I2C_CONTROLLER, SCB0), which is
*                    shared with the display panel.
*
*******************************************************************************/
#include <math.h>

#include "cybsp.h"
#include "mtb_hal.h"
#include "mtb_xensiv_dps3xx.h"

#include "barometer.h"

/*******************************************************************************
* Macros
*******************************************************************************/
/* Exponent of the international barometric formula: 1 / 5.255 */
#define BARO_FORMULA_EXPONENT       (0.190295f)
#define BARO_FORMULA_SCALE_M        (44330.0f)

/* DPS368 timeouts used by the library */
#define BARO_DATA_TIMEOUT_MS        (500U)
#define BARO_I2C_TIMEOUT_MS         (10U)

/* Returned by baro_update() when baro_init() has not succeeded */
#define BARO_RSLT_NOT_READY         (CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, 0U, 1U))

/*******************************************************************************
* Global Variables
*******************************************************************************/
static mtb_hal_i2c_t baro_i2c_hal_obj;
static xensiv_dps3xx_t baro_sensor;

static bool  baro_ready = false;
static bool  baro_filter_primed = false;
static float baro_filtered_hpa = 0.0f;
static float baro_sea_level_hpa = BARO_SEA_LEVEL_HPA;
static float baro_zero_altitude_m = 0.0f;
static volatile bool baro_zero_request = true;   /* zero on first sample */


/*******************************************************************************
* Function Name: baro_pressure_to_altitude
********************************************************************************
* Summary:
*  International barometric formula (troposphere, ISA):
*  h = 44330 * (1 - (p / p0) ^ (1 / 5.255))
*
*******************************************************************************/
float baro_pressure_to_altitude(float pressure_hpa, float sea_level_hpa)
{
    if ((pressure_hpa <= 0.0f) || (sea_level_hpa <= 0.0f))
    {
        return 0.0f;
    }

    return BARO_FORMULA_SCALE_M *
           (1.0f - powf(pressure_hpa / sea_level_hpa, BARO_FORMULA_EXPONENT));
}


/*******************************************************************************
* Function Name: baro_init
********************************************************************************
* Summary:
*  Sets up the HAL I2C object on top of an already initialized and enabled
*  PDL SCB I2C controller, initializes the DPS368 and configures it for
*  background pressure + temperature measurement with high pressure precision.
*
* Parameters:
*  i2c_base    : SCB block of the I2C controller (already enabled)
*  i2c_context : PDL I2C context of that controller
*
* Return:
*  CY_RSLT_SUCCESS when the sensor responded and was configured.
*
*******************************************************************************/
cy_rslt_t baro_init(CySCB_Type* i2c_base, cy_stc_scb_i2c_context_t* i2c_context)
{
    cy_rslt_t result;
    xensiv_dps3xx_config_t config;

    CY_UNUSED_PARAMETER(i2c_base);

    baro_ready = false;

    result = mtb_hal_i2c_setup(&baro_i2c_hal_obj, &CYBSP_I2C_CONTROLLER_hal_config,
                               i2c_context, NULL);
    if (CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    /* KIT_PSE84 boards strap the DPS368 SDO pin high -> address 0x77 */
    result = mtb_xensiv_dps3xx_init_i2c(&baro_sensor, &baro_i2c_hal_obj,
                                        XENSIV_DPS3XX_I2C_ADDR_DEFAULT);
    if (CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    /* Stop measurements before changing the oversampling / rate */
    result = xensiv_dps3xx_get_config(&baro_sensor, &config);
    if (CY_RSLT_SUCCESS == result)
    {
        config.dev_mode = XENSIV_DPS3XX_MODE_IDLE;
        result = xensiv_dps3xx_set_config(&baro_sensor, &config);
    }

    if (CY_RSLT_SUCCESS == result)
    {
        /* Pressure: 16x oversampling (~0.35 Pa RMS, ~3 cm), 8 Hz.
         * Temperature: 1x oversampling, 1 Hz (only used for compensation).
         * Total measurement time ~ 8 x 27.6 ms + 3.6 ms < 1 s as required. */
        config.pressure_oversample    = XENSIV_DPS3XX_OVERSAMPLE_16;
        config.pressure_rate          = XENSIV_DPS3XX_RATE_8;
        config.temperature_oversample = XENSIV_DPS3XX_OVERSAMPLE_1;
        config.temperature_rate       = XENSIV_DPS3XX_RATE_1;
        config.data_timeout           = BARO_DATA_TIMEOUT_MS;
        config.i2c_timeout            = BARO_I2C_TIMEOUT_MS;
        config.dev_mode               = XENSIV_DPS3XX_MODE_BACKGROUND_ALL;
        result = xensiv_dps3xx_set_config(&baro_sensor, &config);
    }

    if (CY_RSLT_SUCCESS == result)
    {
        baro_filter_primed = false;
        baro_zero_request = true;
        baro_ready = true;
    }

    return result;
}


/*******************************************************************************
* Function Name: baro_update
********************************************************************************
* Summary:
*  Reads the latest pressure / temperature, filters the pressure and computes
*  absolute and relative altitude.
*
*******************************************************************************/
cy_rslt_t baro_update(baro_data_t* data)
{
    float pressure_hpa = 0.0f;
    float temperature_c = 0.0f;
    cy_rslt_t result;

    if (!baro_ready)
    {
        return BARO_RSLT_NOT_READY;
    }

    result = xensiv_dps3xx_read(&baro_sensor, &pressure_hpa, &temperature_c);
    if (CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    if (!baro_filter_primed)
    {
        baro_filtered_hpa = pressure_hpa;
        baro_filter_primed = true;
    }
    else
    {
        baro_filtered_hpa += BARO_FILTER_ALPHA * (pressure_hpa - baro_filtered_hpa);
    }

    data->pressure_hpa  = baro_filtered_hpa;
    data->temperature_c = temperature_c;
    data->altitude_m    = baro_pressure_to_altitude(baro_filtered_hpa,
                                                    baro_sea_level_hpa);

    if (baro_zero_request)
    {
        baro_zero_altitude_m = data->altitude_m;
        baro_zero_request = false;
    }

    data->rel_altitude_m = data->altitude_m - baro_zero_altitude_m;

    return CY_RSLT_SUCCESS;
}


/*******************************************************************************
* Function Name: baro_zero_relative
********************************************************************************
* Summary:
*  Requests that the next sample becomes the 0 m reference for the relative
*  altitude (e.g. at the top of a run). Safe to call from an ISR.
*
*******************************************************************************/
void baro_zero_relative(void)
{
    baro_zero_request = true;
}


/*******************************************************************************
* Function Name: baro_set_sea_level
********************************************************************************
* Summary:
*  Updates the reference sea level pressure (QNH) in hPa, e.g. after a GNSS fix.
*
*******************************************************************************/
void baro_set_sea_level(float qnh_hpa)
{
    if (qnh_hpa > 0.0f)
    {
        baro_sea_level_hpa = qnh_hpa;
    }
}


/*******************************************************************************
* Function Name: baro_is_ready
*******************************************************************************/
bool baro_is_ready(void)
{
    return baro_ready;
}

/* [] END OF FILE */
