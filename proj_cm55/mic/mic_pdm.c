/*******************************************************************************
* File Name        : mic_pdm.c
*
* Description      : Stereo PDM microphone capture. See mic_pdm.h.
*
* Hardware chain (per channel, all in the PDM-PCM block):
*   PDM bit stream -> CIC /16 -> FIR0 /3 -> FIR1 /2 -> 24-bit FIFO (64 words)
*   Total decimation 96, same as the BSP default (CIC /32, FIR1 /3), so the
*   output rate stays 16 kHz with the BSP clock divider (clockDiv = 7).
*   The filter settings are the 24-bit configuration used by Infineon's
*   mains-powered-local-voice example.
*
* NOTE: Not verified on hardware yet.
*
*******************************************************************************/
#include "mic_pdm.h"
#include "mic_config.h"

#include <string.h>

#include "cy_pdl.h"
#include "cybsp.h"

#if (MIC_ENABLE_PDM)

/*******************************************************************************
* Macros
*******************************************************************************/
#define PDM_LEFT_CH                 (2U)    /* PDM receiver 2: U8, left  */
#define PDM_RIGHT_CH                (3U)    /* PDM receiver 3: U7, right */

/* FIFO interrupt when it holds more than 31 words -> 32 samples = 2 ms */
#define PDM_FIFO_TRIG_LEVEL         (31U)
#define PDM_SAMPLES_PER_IRQ         (PDM_FIFO_TRIG_LEVEL + 1U)

/* Hardware gain (FIR1 scale). Value used with 24-bit words by the official
 * voice example (AFE_MIC_INPUT_GAIN_DB = 15 -> 17 dB step). */
#ifndef MIC_PDM_HW_GAIN
#define MIC_PDM_HW_GAIN             CY_PDM_PCM_SEL_GAIN_17DB
#endif

#define PDM_ERROR_MASK              (CY_PDM_PCM_INTR_RX_FIR_OVERFLOW | \
                                     CY_PDM_PCM_INTR_RX_OVERFLOW     | \
                                     CY_PDM_PCM_INTR_RX_IF_OVERFLOW  | \
                                     CY_PDM_PCM_INTR_RX_UNDERFLOW)


/*******************************************************************************
* Global Variables
*******************************************************************************/
/* 24-bit channel configuration (overrides the 16-bit BSP default). */
static const cy_stc_pdm_pcm_channel_config_t pdm_ch_cfg_left =
{
    .sampledelay        = 1U,
    .wordSize           = CY_PDM_PCM_WSIZE_24_BIT,
    .signExtension      = true,
    .rxFifoTriggerLevel = PDM_FIFO_TRIG_LEVEL,
    .fir0_enable        = true,
    .cic_decim_code     = CY_PDM_PCM_CHAN_CIC_DECIM_16,
    .fir0_decim_code    = CY_PDM_PCM_CHAN_FIR0_DECIM_3,
    .fir0_scale         = 16U,
    .fir1_decim_code    = CY_PDM_PCM_CHAN_FIR1_DECIM_2,
    .fir1_scale         = 4U,
    .dc_block_disable   = true,     /* DC removed in software (mic_level.c) */
    .dc_block_code      = CY_PDM_PCM_CHAN_DCBLOCK_CODE_16,
};

/* Right channel samples on the other clock edge -> different sample delay */
static const cy_stc_pdm_pcm_channel_config_t pdm_ch_cfg_right =
{
    .sampledelay        = 5U,
    .wordSize           = CY_PDM_PCM_WSIZE_24_BIT,
    .signExtension      = true,
    .rxFifoTriggerLevel = PDM_FIFO_TRIG_LEVEL,
    .fir0_enable        = true,
    .cic_decim_code     = CY_PDM_PCM_CHAN_CIC_DECIM_16,
    .fir0_decim_code    = CY_PDM_PCM_CHAN_FIR0_DECIM_3,
    .fir0_scale         = 16U,
    .fir1_decim_code    = CY_PDM_PCM_CHAN_FIR1_DECIM_2,
    .fir1_scale         = 4U,
    .dc_block_disable   = true,
    .dc_block_code      = CY_PDM_PCM_CHAN_DCBLOCK_CODE_16,
};

static const cy_stc_sysint_t pdm_irq_cfg =
{
    /* Interrupt is taken on the right channel; left is read in the same ISR */
    .intrSrc      = (IRQn_Type) CYBSP_PDM_CHANNEL_3_IRQ,
    .intrPriority = MIC_PDM_IRQ_PRIORITY,
};

/* Ping-pong frame buffers [buffer][channel][sample] */
static int16_t pdm_frames[2][2][MIC_FRAME_SAMPLES];
static volatile uint8_t  pdm_fill_buf;          /* buffer the ISR writes */
static volatile uint32_t pdm_fill_pos;
static volatile int8_t   pdm_ready_buf = -1;    /* newest complete buffer */
static volatile uint32_t pdm_overrun_count;
static volatile uint32_t pdm_error_count;
static volatile bool     pdm_running;
static mic_frame_cb_t    pdm_cb;


/*******************************************************************************
* Function Name: pdm_to_16
********************************************************************************
* Summary:
*  24-bit FIFO word -> 16-bit sample with software gain (AN242253).
*******************************************************************************/
static inline int16_t pdm_to_16(uint32_t fifo_word)
{
    int32_t in = (int32_t) fifo_word;   /* sign-extended by the hardware */
    int32_t out = 0;

    (void) Cy_PDM_PCM_ApplyPCM_Gain(&in, MIC_PDM_SW_GAIN_DB, CY_PDM_PCM_16BIT, &out);
    return (int16_t) out;               /* lower 16 bits hold the sample */
}


/*******************************************************************************
* Function Name: pdm_isr
*******************************************************************************/
static void pdm_isr(void)
{
    uint32_t status = Cy_PDM_PCM_Channel_GetInterruptStatusMasked(PDM0, PDM_RIGHT_CH);

    if (0U != (CY_PDM_PCM_INTR_RX_TRIGGER & status))
    {
        uint32_t n_l = Cy_PDM_PCM_Channel_GetNumInFifo(PDM0, PDM_LEFT_CH);
        uint32_t n_r = Cy_PDM_PCM_Channel_GetNumInFifo(PDM0, PDM_RIGHT_CH);
        uint32_t n = (n_l < n_r) ? n_l : n_r;
        uint8_t  b = pdm_fill_buf;
        uint32_t pos = pdm_fill_pos;

        for (uint32_t i = 0U; i < n; i++)
        {
            int16_t l = pdm_to_16(Cy_PDM_PCM_Channel_ReadFifo(PDM0, PDM_LEFT_CH));
            int16_t r = pdm_to_16(Cy_PDM_PCM_Channel_ReadFifo(PDM0, PDM_RIGHT_CH));

            pdm_frames[b][0][pos] = l;
            pdm_frames[b][1][pos] = r;
            pos++;

            if (pos >= MIC_FRAME_SAMPLES)
            {
                /* Frame complete: publish it, continue in the other buffer */
                if (pdm_ready_buf >= 0)
                {
                    pdm_overrun_count++;
                }
                pdm_ready_buf = (int8_t) b;
                b ^= 1U;
                pos = 0U;
                if (NULL != pdm_cb)
                {
                    pdm_cb();
                }
            }
        }

        pdm_fill_buf = b;
        pdm_fill_pos = pos;
        Cy_PDM_PCM_Channel_ClearInterrupt(PDM0, PDM_RIGHT_CH, CY_PDM_PCM_INTR_RX_TRIGGER);
    }

    if (0U != (PDM_ERROR_MASK & status))
    {
        pdm_error_count++;
        Cy_PDM_PCM_Channel_ClearInterrupt(PDM0, PDM_RIGHT_CH, CY_PDM_PCM_INTR_MASK);
    }
}


/*******************************************************************************
* Function Name: mic_pdm_init
*******************************************************************************/
cy_rslt_t mic_pdm_init(mic_frame_cb_t frame_ready_cb)
{
    pdm_cb = frame_ready_cb;

    /* Block level settings (clock divider, routing) come from the BSP /
     * design.modus personality "CYBSP_PDM". */
    if (CY_PDM_PCM_SUCCESS != Cy_PDM_PCM_Init(PDM0, &CYBSP_PDM_config))
    {
        return MIC_RSLT_ERROR;
    }

    Cy_PDM_PCM_Channel_Enable(PDM0, PDM_LEFT_CH);
    Cy_PDM_PCM_Channel_Enable(PDM0, PDM_RIGHT_CH);

    if ((CY_PDM_PCM_SUCCESS != Cy_PDM_PCM_Channel_Init(PDM0, &pdm_ch_cfg_left, PDM_LEFT_CH)) ||
        (CY_PDM_PCM_SUCCESS != Cy_PDM_PCM_Channel_Init(PDM0, &pdm_ch_cfg_right, PDM_RIGHT_CH)))
    {
        return MIC_RSLT_ERROR;
    }

    (void) Cy_PDM_PCM_SetGain(PDM0, PDM_LEFT_CH, MIC_PDM_HW_GAIN);
    (void) Cy_PDM_PCM_SetGain(PDM0, PDM_RIGHT_CH, MIC_PDM_HW_GAIN);

    Cy_PDM_PCM_Channel_ClearInterrupt(PDM0, PDM_RIGHT_CH, CY_PDM_PCM_INTR_MASK);
    Cy_PDM_PCM_Channel_SetInterruptMask(PDM0, PDM_RIGHT_CH, CY_PDM_PCM_INTR_MASK);

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&pdm_irq_cfg, &pdm_isr))
    {
        return MIC_RSLT_ERROR;
    }
    NVIC_ClearPendingIRQ(pdm_irq_cfg.intrSrc);
    NVIC_EnableIRQ(pdm_irq_cfg.intrSrc);

    return CY_RSLT_SUCCESS;
}


/*******************************************************************************
* Function Name: mic_pdm_start
*******************************************************************************/
void mic_pdm_start(void)
{
    if (!pdm_running)
    {
        pdm_fill_pos = 0U;
        pdm_ready_buf = -1;
        Cy_PDM_PCM_Activate_Channel(PDM0, PDM_LEFT_CH);
        Cy_PDM_PCM_Activate_Channel(PDM0, PDM_RIGHT_CH);
        pdm_running = true;
    }
}


/*******************************************************************************
* Function Name: mic_pdm_stop
*******************************************************************************/
void mic_pdm_stop(void)
{
    if (pdm_running)
    {
        Cy_PDM_PCM_DeActivate_Channel(PDM0, PDM_LEFT_CH);
        Cy_PDM_PCM_DeActivate_Channel(PDM0, PDM_RIGHT_CH);
        pdm_running = false;
    }
}


/*******************************************************************************
* Function Name: mic_pdm_is_running
*******************************************************************************/
bool mic_pdm_is_running(void)
{
    return pdm_running;
}


/*******************************************************************************
* Function Name: mic_pdm_read_frame
*******************************************************************************/
bool mic_pdm_read_frame(int16_t *left, int16_t *right)
{
    int8_t b;
    uint32_t irq;

    irq = Cy_SysLib_EnterCriticalSection();
    b = pdm_ready_buf;
    pdm_ready_buf = -1;
    Cy_SysLib_ExitCriticalSection(irq);

    if (b < 0)
    {
        return false;
    }

    /* The ISR is now filling the other buffer: 10 ms to copy this one */
    (void) memcpy(left,  pdm_frames[b][0], sizeof(pdm_frames[0][0]));
    (void) memcpy(right, pdm_frames[b][1], sizeof(pdm_frames[0][1]));
    return true;
}


uint32_t mic_pdm_overruns(void)   { return pdm_overrun_count; }
uint32_t mic_pdm_fifo_errors(void) { return pdm_error_count; }

#else /* MIC_ENABLE_PDM == 0 */

cy_rslt_t mic_pdm_init(mic_frame_cb_t frame_ready_cb) { (void) frame_ready_cb; return MIC_RSLT_ERROR; }
void mic_pdm_start(void) { }
void mic_pdm_stop(void) { }
bool mic_pdm_is_running(void) { return false; }
bool mic_pdm_read_frame(int16_t *left, int16_t *right) { (void) left; (void) right; return false; }
uint32_t mic_pdm_overruns(void) { return 0U; }
uint32_t mic_pdm_fifo_errors(void) { return 0U; }

#endif /* MIC_ENABLE_PDM */

/* [] END OF FILE */
