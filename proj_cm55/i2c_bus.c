/*******************************************************************************
* File Name        : i2c_bus.c
*
* Description      : Shared I2C bus mutex. See i2c_bus.h.
*
*******************************************************************************/
#include "i2c_bus.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static SemaphoreHandle_t i2c_bus_mutex = NULL;

void i2c_bus_init(void)
{
    if (NULL == i2c_bus_mutex)
    {
        i2c_bus_mutex = xSemaphoreCreateRecursiveMutex();
    }
}

bool i2c_bus_lock(uint32_t timeout_ms)
{
    TickType_t ticks;

    if (NULL == i2c_bus_mutex)
    {
        /* Before the scheduler runs there is only one context: allow it */
        return (taskSCHEDULER_NOT_STARTED == xTaskGetSchedulerState());
    }
    ticks = (I2C_BUS_WAIT_FOREVER == timeout_ms) ? portMAX_DELAY
                                                 : pdMS_TO_TICKS(timeout_ms);
    return (pdTRUE == xSemaphoreTakeRecursive(i2c_bus_mutex, ticks));
}

void i2c_bus_unlock(void)
{
    if (NULL != i2c_bus_mutex)
    {
        (void)xSemaphoreGiveRecursive(i2c_bus_mutex);
    }
}
