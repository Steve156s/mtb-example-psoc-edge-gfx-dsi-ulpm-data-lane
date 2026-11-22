/*******************************************************************************
* File Name        : sht40_i2c_hal_psoc.c
*
* Description      : Implementation of the Sensirion embedded I2C HAL
*                    (sensirion/sensirion_i2c_hal.h) on top of the PSOC Edge
*                    PDL SCB I2C controller, using the high-level interrupt
*                    driven master API. Each read / write is one complete
*                    I2C transaction ending with STOP, as the SHT4x needs.
*
*                    The bus is shared with the display and other sensors.
*                    A module-local mutex serializes the SHT40 accesses; when
*                    the sensor PRs are merged, replace sht40_bus_lock() /
*                    sht40_bus_unlock() with the project-wide I2C mutex.
*
*******************************************************************************/
#include <stddef.h>

#include "sht40_i2c_hal_psoc.h"
#include "sht40_config.h"
#include "sensirion_i2c_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Sensirion HAL return codes: 0 = success, negative = error */
#define HAL_OK                  (0)
#define HAL_ERR_BUS             (-1)
#define HAL_ERR_TIMEOUT         (-2)
#define HAL_ERR_LOCK            (-3)

static CySCB_Type* hal_base = NULL;
static cy_stc_scb_i2c_context_t* hal_ctx = NULL;
static SemaphoreHandle_t hal_mutex = NULL;
static StaticSemaphore_t hal_mutex_storage;


cy_rslt_t sht40_i2c_hal_bind(CySCB_Type* base, cy_stc_scb_i2c_context_t* context)
{
    if ((NULL == base) || (NULL == context))
    {
        return SHT40_HAL_RSLT_ERR;
    }

    hal_base = base;
    hal_ctx = context;

    if (NULL == hal_mutex)
    {
        hal_mutex = xSemaphoreCreateMutexStatic(&hal_mutex_storage);
    }

    return (NULL != hal_mutex) ? CY_RSLT_SUCCESS : SHT40_HAL_RSLT_ERR;
}


static bool sht40_bus_lock(void)
{
    return (pdTRUE == xSemaphoreTake(hal_mutex, pdMS_TO_TICKS(SHT40_I2C_LOCK_TIMEOUT_MS)));
}

static void sht40_bus_unlock(void)
{
    (void)xSemaphoreGive(hal_mutex);
}


/* Waits for the pending high-level transfer. Returns HAL_OK, HAL_ERR_BUS
 * (NAK / arbitration lost / bus error) or HAL_ERR_TIMEOUT. */
static int8_t wait_xfer_done(bool is_read)
{
    TickType_t start = xTaskGetTickCount();
    uint32_t status;

    for (;;)
    {
        status = Cy_SCB_I2C_MasterGetStatus(hal_base, hal_ctx);

        if (0UL == (status & CY_SCB_I2C_MASTER_BUSY))
        {
            break;
        }

        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(SHT40_I2C_TIMEOUT_MS))
        {
            if (is_read)
            {
                Cy_SCB_I2C_MasterAbortRead(hal_base, hal_ctx);
            }
            else
            {
                Cy_SCB_I2C_MasterAbortWrite(hal_base, hal_ctx);
            }
            return HAL_ERR_TIMEOUT;
        }

        taskYIELD();
    }

    return (0UL != (status & CY_SCB_I2C_MASTER_ERR)) ? HAL_ERR_BUS : HAL_OK;
}


static int8_t do_xfer(uint8_t address, uint8_t* data, uint8_t count, bool is_read)
{
    cy_stc_scb_i2c_master_xfer_config_t xfer;
    cy_en_scb_i2c_status_t rc;
    int8_t result;

    if ((NULL == hal_base) || (NULL == data) || (0U == count))
    {
        return HAL_ERR_BUS;
    }

    if (!sht40_bus_lock())
    {
        return HAL_ERR_LOCK;
    }

    xfer.slaveAddress = address;
    xfer.buffer       = data;
    xfer.bufferSize   = count;
    xfer.xferPending  = false;      /* generate STOP */

    rc = is_read ? Cy_SCB_I2C_MasterRead(hal_base, &xfer, hal_ctx)
                 : Cy_SCB_I2C_MasterWrite(hal_base, &xfer, hal_ctx);

    result = (CY_SCB_I2C_SUCCESS == rc) ? wait_xfer_done(is_read) : HAL_ERR_BUS;

    sht40_bus_unlock();

    return result;
}


/*******************************************************************************
* Sensirion HAL API (sensirion_i2c_hal.h)
*******************************************************************************/
int16_t sensirion_i2c_hal_select_bus(uint8_t bus_idx)
{
    /* Single bus: only index 0 is valid */
    return (0U == bus_idx) ? 0 : -1;
}

void sensirion_i2c_hal_init(void)
{
    /* Nothing to do: the controller is initialized by main.c and bound with
     * sht40_i2c_hal_bind(). */
}

void sensirion_i2c_hal_free(void)
{
    /* The controller is shared and owned by main.c: never disable it here */
}

int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t* data, uint8_t count)
{
    return do_xfer(address, data, count, true);
}

int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t* data, uint8_t count)
{
    /* The PDL transfer descriptor is not const-qualified; the buffer is only
     * read for a write transfer. */
    return do_xfer(address, (uint8_t*)(uintptr_t)data, count, false);
}

void sensirion_i2c_hal_sleep_usec(uint32_t useconds)
{
    if (useconds >= 1000U)
    {
        /* Round up and add one tick so the wait is never shorter than
         * requested (the SHT4x measurement must have finished). */
        vTaskDelay(pdMS_TO_TICKS((useconds + 999U) / 1000U) + 1U);
    }
    else
    {
        Cy_SysLib_DelayUs((uint16_t)useconds);
    }
}

/* [] END OF FILE */
