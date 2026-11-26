/*******************************************************************************
* File Name        : stt_poc_main.c
*
* Description      : CM55 main() for the standalone STT -> LoRa POC on
*                    KIT_PSE84_AI. Replaces proj_cm55/main.c.
*
*   - PDM left mic (16 kHz) -> stt_link PTT recorder
*   - hold USER BTN, speak, release -> audio to the PC (KitProg3 UART)
*   - PC STT text -> Heltec UART (J14 P16.0 RX / P16.1 TX, SCB10, 3.3 V)
*
*   STT_POC_USE_MIC = 0 builds without the microphone (text from the PC only:
*   stt_bridge.py --typed or --pc-mic).
*
* Device Configurator (design.modus) before building:
*   - P16.0 / P16.1: untick CYBSP_DVP_CAM_D0 / D1, enable SCB10 as UART
*     115200 8N1, name the personality HELTEC_UART
*   - Debug UART (CYBSP_DEBUG_UART): 921600 baud
*   - System Idle Power Mode: CPU Sleep (not Deep Sleep), so PDM and UART
*     keep running between RTOS ticks
*
* Not built or run on hardware yet.
*******************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "mtb_hal.h"
#include "cy_time.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"

#include "retarget_io_init.h"
#include "stt_link.h"

#ifndef STT_POC_USE_MIC
#define STT_POC_USE_MIC                 (1)
#endif

#if (STT_POC_USE_MIC)
#include "mic_config.h"
#include "mic_pdm.h"
#endif

#define STT_LINK_TASK_PRIORITY          (configMAX_PRIORITIES - 3)
#define MIC_TASK_PRIORITY               (configMAX_PRIORITIES - 2)
#define MIC_TASK_STACK_WORDS            (512U)

#define APP_LPTIMER_INTERRUPT_PRIORITY  (1U)
#define LPTIMER_1_WAIT_TIME_USEC        (62U)

static mtb_hal_lptimer_t lptimer_obj;
static mtb_hal_rtc_t     rtc_obj;

static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}

/* Same as the example's tickless setup; needed when FreeRTOSConfig.h enables
 * tickless idle. */
static void setup_tickless_idle_timer(void)
{
    cy_stc_sysint_t lptimer_intr_cfg =
    {
        .intrSrc = CYBSP_CM55_LPTIMER_1_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&lptimer_intr_cfg, lptimer_interrupt_handler))
    {
        handle_app_error();
    }
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    if (CY_MCWDT_SUCCESS != Cy_MCWDT_Init(CYBSP_CM55_LPTIMER_1_HW, &CYBSP_CM55_LPTIMER_1_config))
    {
        handle_app_error();
    }
    Cy_MCWDT_Enable(CYBSP_CM55_LPTIMER_1_HW, CY_MCWDT_CTR_Msk, LPTIMER_1_WAIT_TIME_USEC);

    if (CY_RSLT_SUCCESS != mtb_hal_lptimer_setup(&lptimer_obj, &CYBSP_CM55_LPTIMER_1_hal_config))
    {
        handle_app_error();
    }
    cyabs_rtos_set_lptimer(&lptimer_obj);
}

#if (STT_POC_USE_MIC)
static TaskHandle_t mic_task_handle;

/* PDM ISR: a 10 ms frame is ready */
static void on_mic_frame(void)
{
    BaseType_t woken = pdFALSE;
    if (NULL != mic_task_handle)
    {
        vTaskNotifyGiveFromISR(mic_task_handle, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static void mic_task(void *arg)
{
    static int16_t left[MIC_FRAME_SAMPLES];
    static int16_t right[MIC_FRAME_SAMPLES];
    (void)arg;

    mic_pdm_start();
    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        while (mic_pdm_read_frame(left, right))
        {
            stt_link_push_pcm(left, MIC_FRAME_SAMPLES);   /* left mic only */
        }
    }
}
#endif

int main(void)
{
    if (CY_RSLT_SUCCESS != cybsp_init())
    {
        handle_app_error();
    }

    init_retarget_io();
    mtb_clib_support_init(&rtc_obj);      /* RTC is set up by CM33 NS */
    setup_tickless_idle_timer();
    __enable_irq();

    printf("\r\n*** Tracker STT -> LoRa POC (CM55) ***\r\n");

#if (STT_POC_USE_MIC)
    if (CY_RSLT_SUCCESS != mic_pdm_init(on_mic_frame))
    {
        printf("[POC] PDM init failed, continuing without mic\r\n");
    }
    else if (pdPASS != xTaskCreate(mic_task, "mic", MIC_TASK_STACK_WORDS, NULL,
                                   MIC_TASK_PRIORITY, &mic_task_handle))
    {
        handle_app_error();
    }
#endif

    if (CY_RSLT_SUCCESS != stt_link_start(STT_LINK_TASK_PRIORITY))
    {
        printf("[POC] stt_link_start failed (HELTEC_UART configured?)\r\n");
        handle_app_error();
    }

    vTaskStartScheduler();
    handle_app_error();
    return 0;
}
