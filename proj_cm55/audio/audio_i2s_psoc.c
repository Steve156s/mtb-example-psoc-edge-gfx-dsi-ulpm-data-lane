/*******************************************************************************
* File Name        : audio_i2s_psoc.c
*
* Description      : I2S (TDM0 TX) ping-pong streaming, see header.
*                    TDM calls follow Infineon mtb-example-psoc-edge-i2s
*                    (proj_cm33_ns/source/app_i2s/app_i2s.c), moved to CM55.
*
*******************************************************************************/
#include <string.h>

#include "cy_pdl.h"
#include "cybsp.h"

#include "audio_i2s_psoc.h"
#include "audio_config.h"

#if (0U != (AUDIO_BLOCK_SAMPLES % AUDIO_FIFO_HALF))
#error "AUDIO_BLOCK_SAMPLES must be a multiple of AUDIO_FIFO_HALF"
#endif

#define I2S_TX              (TDM_STRUCT0_TX)

static const cy_stc_sysint_t i2s_irq_cfg =
{
    .intrSrc      = (IRQn_Type)tdm_0_interrupts_tx_0_IRQn,
    .intrPriority = AUDIO_I2S_IRQ_PRIORITY,
};

static int16_t block[2][AUDIO_BLOCK_SAMPLES];
static volatile bool block_ready[2];
static volatile uint8_t rd_block;
static volatile uint32_t rd_pos;
static volatile uint32_t underruns;
static volatile bool running;
static TaskHandle_t notify_task;


static void i2s_tx_isr(void)
{
    uint32_t status = Cy_AudioTDM_GetTxInterruptStatusMasked(I2S_TX);
    BaseType_t woken = pdFALSE;

    if (0UL != (status & CY_TDM_INTR_TX_FIFO_TRIGGER))
    {
        uint32_t i;

        for (i = 0U; i < AUDIO_FIFO_HALF; i++)
        {
            int16_t s = 0;
            uint8_t b = rd_block;

            if (block_ready[b])
            {
                s = block[b][rd_pos++];

                if (rd_pos >= AUDIO_BLOCK_SAMPLES)
                {
                    rd_pos = 0U;
                    block_ready[b] = false;
                    rd_block = (uint8_t)(b ^ 1U);
                    if (NULL != notify_task)
                    {
                        vTaskNotifyGiveFromISR(notify_task, &woken);
                    }
                }
            }
            else if (block_ready[b ^ 1U])
            {
                /* Should not happen; resynchronise on the other block */
                rd_block = (uint8_t)(b ^ 1U);
                rd_pos = 0U;
                underruns++;
            }

            /* Same sample on left and right (16-bit words, zero extended) */
            Cy_AudioTDM_WriteTxData(I2S_TX, (uint32_t)(uint16_t)s);
            Cy_AudioTDM_WriteTxData(I2S_TX, (uint32_t)(uint16_t)s);
        }
    }

    if (0UL != (status & CY_TDM_INTR_TX_FIFO_UNDERFLOW))
    {
        underruns++;
    }

    Cy_AudioTDM_ClearTxInterrupt(I2S_TX, CY_TDM_INTR_TX_MASK);
    portYIELD_FROM_ISR(woken);
}


cy_rslt_t audio_i2s_init(TaskHandle_t notify)
{
    cy_en_tdm_status_t rc;

    notify_task = notify;
    memset(block, 0, sizeof(block));
    block_ready[0] = false;
    block_ready[1] = false;
    rd_block = 0U;
    rd_pos = 0U;
    underruns = 0U;
    running = false;

    rc = Cy_AudioTDM_Init(TDM_STRUCT0, &CYBSP_TDM_CONTROLLER_0_config);
    if (CY_TDM_SUCCESS != rc)
    {
        return (cy_rslt_t)rc;
    }

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&i2s_irq_cfg, i2s_tx_isr))
    {
        return CY_RSLT_TYPE_ERROR;
    }
    NVIC_EnableIRQ(i2s_irq_cfg.intrSrc);

    return CY_RSLT_SUCCESS;
}


void audio_i2s_start(void)
{
    uint32_t i;

    if (running)
    {
        return;
    }

    rd_pos = 0U;

    Cy_AudioTDM_EnableTx(I2S_TX);
    Cy_AudioTDM_ClearTxInterrupt(I2S_TX, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_SetTxInterruptMask(I2S_TX, CY_TDM_INTR_TX_MASK);

    /* Prefill half of the FIFO with silence before activation */
    for (i = 0U; i < (2U * AUDIO_FIFO_HALF); i++)
    {
        Cy_AudioTDM_WriteTxData(I2S_TX, 0UL);
    }

    running = true;
    Cy_AudioTDM_ActivateTx(I2S_TX);
}


void audio_i2s_stop(void)
{
    if (!running)
    {
        return;
    }

    /* No direct FIFO clear: disable / enable flushes it, then deactivate */
    Cy_AudioI2S_DisableTx(I2S_TX);
    Cy_AudioI2S_EnableTx(I2S_TX);
    Cy_AudioTDM_DeActivateTx(I2S_TX);
    Cy_AudioI2S_DisableTx(I2S_TX);

    running = false;
    block_ready[0] = false;
    block_ready[1] = false;
    rd_pos = 0U;
}


bool audio_i2s_running(void)
{
    return running;
}


int16_t* audio_i2s_get_free_block(void)
{
    /* Fill the block the ISR reads next first, so playback starts at once */
    uint8_t first = rd_block;

    if (!block_ready[first])
    {
        return block[first];
    }
    if (!block_ready[first ^ 1U])
    {
        return block[first ^ 1U];
    }
    return NULL;
}


void audio_i2s_commit_block(int16_t* blk)
{
    uint8_t b = (blk == block[0]) ? (uint8_t)0U : (uint8_t)1U;

    /* Data must be in memory before the flag (ISR reads it) */
    __DMB();
    block_ready[b] = true;
}


bool audio_i2s_idle(void)
{
    return !block_ready[0] && !block_ready[1];
}


uint32_t audio_i2s_underruns(void)
{
    return underruns;
}

/* [] END OF FILE */
