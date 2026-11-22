/*******************************************************************************
* File Name        : audio_i2c_psoc.h
*
* Description      : tlv320_bus_t implementation on the PSOC Edge PDL SCB I2C
*                    controller (high-level interrupt driven master API).
*
*******************************************************************************/
#ifndef AUDIO_I2C_PSOC_H
#define AUDIO_I2C_PSOC_H

#include "cy_pdl.h"
#include "tlv320dac3100.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Binds the bus to an SCB already initialized and enabled by main.c (with
 * its interrupt calling Cy_SCB_I2C_Interrupt) and returns the bus. */
const tlv320_bus_t* audio_i2c_psoc_bind(CySCB_Type* base, cy_stc_scb_i2c_context_t* context);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_I2C_PSOC_H */
