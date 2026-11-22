/*******************************************************************************
* File Name        : i2c_bus.h
*
* Description      : Mutex for the shared CYBSP_I2C_CONTROLLER bus (display,
*                    BMI270, BMM350, DPS368, ...). Every task that talks on
*                    the bus must wrap each sensor transaction with
*                    i2c_bus_lock() / i2c_bus_unlock().
*
*******************************************************************************/
#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define I2C_BUS_WAIT_FOREVER    (0xFFFFFFFFUL)

/* Create the mutex. Call once from main() before vTaskStartScheduler(). */
void i2c_bus_init(void);

/* Take the bus. Returns false on timeout (or if i2c_bus_init was not called
 * and the scheduler has started). Recursive: the same task may nest calls. */
bool i2c_bus_lock(uint32_t timeout_ms);

void i2c_bus_unlock(void);

#if defined(__cplusplus)
}
#endif

#endif /* I2C_BUS_H */
