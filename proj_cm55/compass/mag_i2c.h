/*******************************************************************************
* File Name        : mag_i2c.h
*
* Description      : Thin, thread-safe register access layer on top of the
*                    PDL SCB I2C controller, used by the Bosch BMM350
*                    SensorAPI glue. Transfers are serialised with a private
*                    mutex. If other tasks later use the same bus through
*                    their own drivers, replace mag_i2c_bus_lock/unlock with
*                    the project-wide bus mutex.
*
*                    The I2C block, its interrupt and its context are owned by
*                    main.c (the same bus carries the display touch controller
*                    and the on-board sensors on KIT_PSE84_EVAL, and the 1.8 V
*                    sensor bus on KIT_PSE84_AI). This module only borrows them.

*
*******************************************************************************/
#ifndef MAG_I2C_H
#define MAG_I2C_H

#include <stdint.h>
#include <stdbool.h>
#include "cy_pdl.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Per-transfer timeout */
#define MAG_I2C_TIMEOUT_MS           (20U)

/* Largest single register burst write supported (excluding register byte).
 * Must be >= the BMI270 read_write_len used when uploading its config file.
 */
#define MAG_I2C_MAX_WRITE_LEN        (64U)

/* Generic error result returned by mag_i2c_init() / compass_start() */
#define MAG_I2C_RSLT_ERR             CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_BASE, 0x350U)

/* Interface descriptor handed to the Bosch APIs through dev->intf_ptr */
typedef struct
{
    uint8_t address;    /* 7-bit I2C address */
} mag_i2c_dev_t;

/* Bus mutex wait per register access */
#define MAG_I2C_LOCK_TIMEOUT_MS      (100U)

/* Must be called after Cy_SCB_I2C_Init()/Enable() and the I2C IRQ have been
 * configured by the application, before the compass task runs. */
cy_rslt_t mag_i2c_init(CySCB_Type *base, cy_stc_scb_i2c_context_t *context);

/* Register read/write: 0 on success, negative on error (Bosch convention). */
int8_t mag_i2c_read_regs(uint8_t address, uint8_t reg, uint8_t *data, uint32_t len);
int8_t mag_i2c_write_regs(uint8_t address, uint8_t reg, const uint8_t *data, uint32_t len);

/* Returns true if a device ACKs its address. */
bool mag_i2c_probe(uint8_t address);

/* Microsecond delay that yields to the RTOS for long waits. */
void mag_i2c_delay_us(uint32_t period_us);

#if defined(__cplusplus)
}
#endif

#endif /* MAG_I2C_H */
