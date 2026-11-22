/*******************************************************************************
* File Name        : sht40_i2c_hal_psoc.h
*
* Description      : Binds the Sensirion I2C HAL (sensirion_i2c_hal.h) to an
*                    already initialized PDL SCB I2C controller.
*
*******************************************************************************/
#ifndef SHT40_I2C_HAL_PSOC_H
#define SHT40_I2C_HAL_PSOC_H

#include <stdbool.h>
#include "cy_pdl.h"
#include "cy_result.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SHT40_HAL_RSLT_ERR      (CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_BASE, 0x440U))

/* Must be called before sensirion_i2c_hal_* are used. The controller (and its
 * interrupt) is owned by the caller and must already be enabled. */
cy_rslt_t sht40_i2c_hal_bind(CySCB_Type* base, cy_stc_scb_i2c_context_t* context);

#if defined(__cplusplus)
}
#endif

#endif /* SHT40_I2C_HAL_PSOC_H */
