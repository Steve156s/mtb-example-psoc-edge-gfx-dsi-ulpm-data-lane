/*******************************************************************************
* File Name        : audio_i2c_psoc.c
*
* Description      : TLV320DAC3100 bus access on the PDL SCB I2C controller.
*
*                    The bus (CYBSP_I2C_CONTROLLER, P8.0 / P8.1) is shared
*                    with the display and the sensors. A module-local mutex
*                    serializes the DAC accesses; when the sensor PRs are
*                    merged, replace bus_lock() / bus_unlock() with the
*                    project-wide I2C mutex.
*
*******************************************************************************/
#include <stddef.h>

#include "audio_i2c_psoc.h"
#include "audio_config.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

typedef struct
{
    CySCB_Type* base;
    cy_stc_scb_i2c_context_t* ctx;
} audio_i2c_t;

static audio_i2c_t i2c_port;
static SemaphoreHandle_t bus_mutex = NULL;
static StaticSemaphore_t bus_mutex_storage;


static bool bus_lock(void)
{
    return (pdTRUE == xSemaphoreTake(bus_mutex, pdMS_TO_TICKS(AUDIO_I2C_LOCK_TIMEOUT_MS)));
}

static void bus_unlock(void)
{
    (void)xSemaphoreGive(bus_mutex);
}


/* Waits for the pending high-level transfer: 0, -1 bus error, -2 timeout */
static int wait_done(const audio_i2c_t* p, bool is_read)
{
    TickType_t start = xTaskGetTickCount();
    uint32_t status;

    for (;;)
    {
        status = Cy_SCB_I2C_MasterGetStatus(p->base, p->ctx);

        if (0UL == (status & CY_SCB_I2C_MASTER_BUSY))
        {
            break;
        }

        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(AUDIO_I2C_TIMEOUT_MS))
        {
            if (is_read)
            {
                Cy_SCB_I2C_MasterAbortRead(p->base, p->ctx);
            }
            else
            {
                Cy_SCB_I2C_MasterAbortWrite(p->base, p->ctx);
            }
            return -2;
        }

        taskYIELD();
    }

    return (0UL != (status & CY_SCB_I2C_MASTER_ERR)) ? -1 : 0;
}


static int xfer(const audio_i2c_t* p, uint8_t addr7, uint8_t* data, uint16_t len, bool is_read)
{
    cy_stc_scb_i2c_master_xfer_config_t x;
    cy_en_scb_i2c_status_t rc;

    x.slaveAddress = addr7;
    x.buffer       = data;
    x.bufferSize   = len;
    x.xferPending  = false;     /* STOP at the end */

    rc = is_read ? Cy_SCB_I2C_MasterRead(p->base, &x, p->ctx)
                 : Cy_SCB_I2C_MasterWrite(p->base, &x, p->ctx);

    return (CY_SCB_I2C_SUCCESS == rc) ? wait_done(p, is_read) : -1;
}


static int bus_write(void* ctx, uint8_t addr7, const uint8_t* data, uint16_t len)
{
    const audio_i2c_t* p = (const audio_i2c_t*)ctx;
    int rc;

    if (!bus_lock())
    {
        return -3;
    }

    /* The PDL descriptor is not const-qualified; a write only reads it */
    rc = xfer(p, addr7, (uint8_t*)(uintptr_t)data, len, false);
    bus_unlock();
    return rc;
}


static int bus_read_reg(void* ctx, uint8_t addr7, uint8_t reg, uint8_t* value)
{
    const audio_i2c_t* p = (const audio_i2c_t*)ctx;
    int rc;

    if (!bus_lock())
    {
        return -3;
    }

    /* Register pointer write, STOP, then read: the DAC keeps the pointer */
    rc = xfer(p, addr7, &reg, 1U, false);
    if (0 == rc)
    {
        rc = xfer(p, addr7, value, 1U, true);
    }
    bus_unlock();
    return rc;
}


static void bus_delay(void* ctx, uint32_t ms)
{
    (void)ctx;
    /* +1 tick: never shorter than requested */
    vTaskDelay(pdMS_TO_TICKS(ms) + 1U);
}


static const tlv320_bus_t tlv_bus =
{
    .write    = bus_write,
    .read_reg = bus_read_reg,
    .delay_ms = bus_delay,
    .ctx      = &i2c_port,
};


const tlv320_bus_t* audio_i2c_psoc_bind(CySCB_Type* base, cy_stc_scb_i2c_context_t* context)
{
    if ((NULL == base) || (NULL == context))
    {
        return NULL;
    }

    i2c_port.base = base;
    i2c_port.ctx = context;

    if (NULL == bus_mutex)
    {
        bus_mutex = xSemaphoreCreateMutexStatic(&bus_mutex_storage);
    }

    return (NULL != bus_mutex) ? &tlv_bus : NULL;
}

/* [] END OF FILE */
