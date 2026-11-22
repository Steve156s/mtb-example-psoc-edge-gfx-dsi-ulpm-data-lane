/*******************************************************************************
* File Name        : mag_i2c.c
*
* Description      : Thread-safe register access over the PDL SCB I2C
*                    controller (high-level, interrupt driven transfers),
*                    serialised with the other bus users through i2c_bus.h.
*
*******************************************************************************/
#include <string.h>

#include "mag_i2c.h"

#include "FreeRTOS.h"
#include "task.h"

#include "i2c_bus.h"

/*******************************************************************************
* Static data
*******************************************************************************/
static CySCB_Type *i2c_base = NULL;
static cy_stc_scb_i2c_context_t *i2c_ctx = NULL;

static uint8_t tx_buf[MAG_I2C_MAX_WRITE_LEN + 1U];


/*******************************************************************************
* Function Name: wait_xfer_done
********************************************************************************
* Summary:
*  Waits for the current high-level master transfer to finish.
*
* Parameters:
*  is_read: true if the pending transfer is a read (selects the abort call).
*
* Return:
*  0 on success, -1 on bus error / NAK, -2 on timeout. (-3 from the callers
*  means the shared bus mutex could not be taken.)
*******************************************************************************/
static int8_t wait_xfer_done(bool is_read)
{
    TickType_t start = xTaskGetTickCount();
    uint32_t status;

    for (;;)
    {
        status = Cy_SCB_I2C_MasterGetStatus(i2c_base, i2c_ctx);

        if (0UL == (status & CY_SCB_I2C_MASTER_BUSY))
        {
            break;
        }

        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(MAG_I2C_TIMEOUT_MS))
        {
            if (is_read)
            {
                Cy_SCB_I2C_MasterAbortRead(i2c_base, i2c_ctx);
            }
            else
            {
                Cy_SCB_I2C_MasterAbortWrite(i2c_base, i2c_ctx);
            }
            return -2;
        }

        taskYIELD();
    }

    return (0UL != (status & CY_SCB_I2C_MASTER_ERR)) ? -1 : 0;
}


cy_rslt_t mag_i2c_init(CySCB_Type *base, cy_stc_scb_i2c_context_t *context)
{
    if ((NULL == base) || (NULL == context))
    {
        return MAG_I2C_RSLT_ERR;
    }

    i2c_base = base;
    i2c_ctx = context;

    return CY_RSLT_SUCCESS;
}


int8_t mag_i2c_read_regs(uint8_t address, uint8_t reg, uint8_t *data, uint32_t len)
{
    cy_stc_scb_i2c_master_xfer_config_t xfer;
    int8_t rslt;

    if ((NULL == i2c_base) || (NULL == data) || (0U == len))
    {
        return -1;
    }

    if (!i2c_bus_lock(MAG_I2C_LOCK_TIMEOUT_MS))
    {
        return -3;
    }

    /* Register pointer write, no STOP (repeated START follows) */
    xfer.slaveAddress = address;
    xfer.buffer       = &reg;
    xfer.bufferSize   = 1U;
    xfer.xferPending  = true;

    rslt = (CY_SCB_I2C_SUCCESS == Cy_SCB_I2C_MasterWrite(i2c_base, &xfer, i2c_ctx)) ? wait_xfer_done(false) : -1;

    if (0 == rslt)
    {
        xfer.buffer      = data;
        xfer.bufferSize  = len;
        xfer.xferPending = false;

        rslt = (CY_SCB_I2C_SUCCESS == Cy_SCB_I2C_MasterRead(i2c_base, &xfer, i2c_ctx)) ? wait_xfer_done(true) : -1;
    }

    i2c_bus_unlock();

    return rslt;
}


int8_t mag_i2c_write_regs(uint8_t address, uint8_t reg, const uint8_t *data, uint32_t len)
{
    cy_stc_scb_i2c_master_xfer_config_t xfer;
    int8_t rslt;

    if ((NULL == i2c_base) || (len > MAG_I2C_MAX_WRITE_LEN) || ((NULL == data) && (0U != len)))
    {
        return -1;
    }

    if (!i2c_bus_lock(MAG_I2C_LOCK_TIMEOUT_MS))
    {
        return -3;
    }

    tx_buf[0] = reg;
    if (0U != len)
    {
        memcpy(&tx_buf[1], data, len);
    }

    xfer.slaveAddress = address;
    xfer.buffer       = tx_buf;
    xfer.bufferSize   = len + 1U;
    xfer.xferPending  = false;

    rslt = (CY_SCB_I2C_SUCCESS == Cy_SCB_I2C_MasterWrite(i2c_base, &xfer, i2c_ctx)) ? wait_xfer_done(false) : -1;

    i2c_bus_unlock();

    return rslt;
}


bool mag_i2c_probe(uint8_t address)
{
    uint8_t dummy;

    /* A register read of address 0x00 (chip ID on both BMM350 and BMI270) */
    return (0 == mag_i2c_read_regs(address, 0x00U, &dummy, 1U));
}


void mag_i2c_delay_us(uint32_t period_us)
{
    if (period_us >= 2000U)
    {
        /* Round up so we never wait shorter than requested */
        vTaskDelay(pdMS_TO_TICKS((period_us + 999U) / 1000U) + 1U);
    }
    else
    {
        Cy_SysLib_DelayUs((uint16_t) period_us);
    }
}
