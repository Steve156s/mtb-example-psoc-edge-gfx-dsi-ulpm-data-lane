/*******************************************************************************
* File Name        : radar_task.c
*
* Description      : See radar_task.h. SPI / sensor bring-up follows the
*                    Infineon code example
*                    mtb-example-psoc-edge-ml-deepcraft-deploy-radar
*                    (radar_init()), but reads one complete frame per
*                    interrupt with xensiv_bgt60trxx_get_fifo_data() and
*                    processes it in this task.
*
*                    NOT YET TESTED ON HARDWARE.
*
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "radar_config.h"
#include "radar_hud.h"
#include "radar_task.h"

/* The radar is only wired on kits whose BSP defines these pins
 * (KIT_PSE84_AI, KIT_PSE84_HMI). */
#if defined(CYBSP_RSPI_CS_PORT) && defined(CYBSP_RADAR_INT_PORT) && \
    defined(CYBSP_RADAR_RESET_PORT) && defined(CYBSP_SPI_CONTROLLER_HW)
#define RADAR_HW_AVAILABLE          (1)
#else
#define RADAR_HW_AVAILABLE          (0)
#endif

#if RADAR_HW_AVAILABLE
#include "xensiv_bgt60trxx_mtb.h"
#include "xensiv_bgt60trxx_platform.h"
#define XENSIV_BGT60TRXX_CONF_IMPL
#include "radar_settings.h"

#if (XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_FRAME != RADAR_DSP_FRAME_SAMPLES)
#error "radar_settings.h does not match the radar_dsp frame geometry"
#endif
#endif

#define RADAR_TASK_NAME             ("Radar Task")
/* stack size in words: DSP uses ~1 KB of locals, snprintf in the log */
#define RADAR_TASK_STACK_SIZE       (configMINIMAL_STACK_SIZE * 8)
#define RADAR_GESTURE_QUEUE_LEN     (4U)

#define RADAR_SPI_INTR_PRIORITY     (2U)
#define RADAR_IRQ_PRIORITY          (2U)
/* No frame interrupt for this long -> sensor considered stuck, restart */
#define RADAR_FRAME_TIMEOUT_MS      (500U)


/*******************************************************************************
* Global Variables
*******************************************************************************/
static radar_state_t     radar_state;
static QueueHandle_t     gesture_queue = NULL;

#if RADAR_HW_AVAILABLE
static TaskHandle_t             radar_task_handle = NULL;
static radar_dsp_t              dsp;
static radar_gesture_ctx_t      gesture_ctx;
static cy_stc_scb_spi_context_t radar_spi_context;
static xensiv_bgt60trxx_mtb_t   bgt60_obj;
static cy_stc_sysint_t          radar_irq_cfg;

/* One frame of raw 12-bit samples (12 KB) */
static uint16_t radar_frame[XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_FRAME];
#endif


/*******************************************************************************
* Weak hooks
*******************************************************************************/
__attribute__((weak)) void radar_port_on_gesture(radar_gesture_t gesture)
{
    (void)gesture;
}


/*******************************************************************************
* Public state access
*******************************************************************************/
void radar_task_get_state(radar_state_t* state)
{
    taskENTER_CRITICAL();
    *state = radar_state;
    taskEXIT_CRITICAL();
}


bool radar_task_get_gesture(radar_gesture_t* gesture)
{
    if (NULL == gesture_queue)
    {
        return false;
    }
    return (pdTRUE == xQueueReceive(gesture_queue, gesture, 0));
}


#if RADAR_HW_AVAILABLE
static void set_status(radar_status_t status)
{
    taskENTER_CRITICAL();
    radar_state.status = status;
    taskEXIT_CRITICAL();
}


/*******************************************************************************
* Interrupt handlers
*******************************************************************************/
static void radar_spi_interrupt(void)
{
    Cy_SCB_SPI_Interrupt(CYBSP_SPI_CONTROLLER_HW, &radar_spi_context);
}


static void radar_fifo_interrupt(void)
{
    BaseType_t woken = pdFALSE;

    Cy_GPIO_ClearInterrupt(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_PIN);
    NVIC_ClearPendingIRQ(radar_irq_cfg.intrSrc);

    if (NULL != radar_task_handle)
    {
        vTaskNotifyGiveFromISR(radar_task_handle, &woken);
    }
    portYIELD_FROM_ISR(woken);
}


/*******************************************************************************
* Function Name: radar_hw_init
********************************************************************************
* Summary:
*   SPI controller, BGT60TR13C reset + register configuration, FIFO limit of
*   one frame and the FIFO interrupt. Called from the radar task because the
*   driver uses blocking delays.
*******************************************************************************/
static cy_rslt_t radar_hw_init(void)
{
    cy_rslt_t result;
    cy_stc_sysint_t spi_irq_cfg =
    {
        .intrSrc      = CYBSP_SPI_CONTROLLER_IRQ,
        .intrPriority = RADAR_SPI_INTR_PRIORITY,
    };

    if (CY_SCB_SPI_SUCCESS != Cy_SCB_SPI_Init(CYBSP_SPI_CONTROLLER_HW,
                                              &CYBSP_SPI_CONTROLLER_config,
                                              &radar_spi_context))
    {
        printf("Radar: SPI init failed\r\n");
        return RADAR_RSLT_TASK_FAILED;
    }
    (void)Cy_SysInt_Init(&spi_irq_cfg, &radar_spi_interrupt);
    NVIC_EnableIRQ(spi_irq_cfg.intrSrc);
    Cy_SCB_SPI_Enable(CYBSP_SPI_CONTROLLER_HW);

    /* Chip select, reset and IRQ are driven as GPIO by the driver */
    bgt60_obj.iface.scb_inst = CYBSP_SPI_CONTROLLER_HW;
    bgt60_obj.iface.spi      = &radar_spi_context;
    bgt60_obj.iface.sel_port = CYBSP_RSPI_CS_PORT;
    bgt60_obj.iface.sel_pin  = CYBSP_RSPI_CS_PIN;
    bgt60_obj.iface.rst_port = CYBSP_RADAR_RESET_PORT;
    bgt60_obj.iface.rst_pin  = CYBSP_RADAR_RESET_PIN;
    bgt60_obj.iface.irq_port = CYBSP_RADAR_INT_PORT;
    bgt60_obj.iface.irq_pin  = CYBSP_RADAR_INT_PIN;
    bgt60_obj.iface.irq_num  = CYBSP_RADAR_INT_IRQ;

#if defined(CYBSP_RSPI_MOSI_PORT) && defined(CYBSP_RSPI_CLK_PORT)
    /* Reduced drive strength for EMI, as in the Infineon example */
    Cy_GPIO_SetSlewRate(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, CY_GPIO_DRIVE_1_8);
    Cy_GPIO_SetSlewRate(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, CY_GPIO_DRIVE_1_8);
#endif

    result = xensiv_bgt60trxx_mtb_init(&bgt60_obj, register_list_gesture,
                                       XENSIV_BGT60TRXX_CONF_NUM_REGS);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("Radar: BGT60TR13C init failed (0x%08lx)\r\n", (unsigned long)result);
        return result;
    }

    /* Interrupt when one complete frame is in the FIFO */
    result = xensiv_bgt60trxx_mtb_interrupt_init(&bgt60_obj,
                                                 XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_FRAME);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("Radar: FIFO interrupt init failed (0x%08lx)\r\n", (unsigned long)result);
        return result;
    }

    radar_irq_cfg.intrSrc = (IRQn_Type)bgt60_obj.iface.irq_num;
    radar_irq_cfg.intrPriority = RADAR_IRQ_PRIORITY;
    (void)Cy_SysInt_Init(&radar_irq_cfg, &radar_fifo_interrupt);
    Cy_GPIO_ClearInterrupt(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_PIN);
    NVIC_ClearPendingIRQ(radar_irq_cfg.intrSrc);
    NVIC_EnableIRQ(radar_irq_cfg.intrSrc);

    return CY_RSLT_SUCCESS;
}


/* Clear the sensor FIFO and (re)start frame generation */
static bool radar_restart_frames(void)
{
    (void)xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, false);
    if (XENSIV_BGT60TRXX_STATUS_OK !=
        xensiv_bgt60trxx_soft_reset(&bgt60_obj.dev, XENSIV_BGT60TRXX_RESET_FIFO))
    {
        return false;
    }
    (void)ulTaskNotifyTake(pdTRUE, 0);
    return (XENSIV_BGT60TRXX_STATUS_OK == xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true));
}


static void log_gesture(radar_gesture_t g, const radar_gesture_ctx_t* ctx)
{
    char daz[12], del[12], drg[12];

    /* newlib-nano printf has no float support: format by hand */
    (void)radar_format_fixed(daz, sizeof(daz), ctx->last_d_az, 1U, true);
    (void)radar_format_fixed(del, sizeof(del), ctx->last_d_el, 1U, true);
    (void)radar_format_fixed(drg, sizeof(drg), ctx->last_d_rng, 1U, true);
    printf("Radar: gesture %s (frames %lu, d_az %s deg, d_el %s deg, d_range %s cm)\r\n",
           radar_gesture_name(g), (unsigned long)ctx->last_frames, daz, del, drg);
}


/*******************************************************************************
* Function Name: radar_task
*******************************************************************************/
static void radar_task(void* arg)
{
    CY_UNUSED_PARAMETER(arg);

    if (CY_RSLT_SUCCESS != radar_hw_init())
    {
        set_status(RADAR_STATUS_ERROR);
        vTaskSuspend(NULL);
    }

    radar_dsp_init(&dsp);
    radar_gesture_init(&gesture_ctx);

    if (!radar_restart_frames())
    {
        printf("Radar: start frame failed\r\n");
        set_status(RADAR_STATUS_ERROR);
        vTaskSuspend(NULL);
    }

    set_status(RADAR_STATUS_RUNNING);
    printf("Radar: BGT60TR13C running, %u samples x %u chirps x %u RX, 33 frames/s\r\n",
           (unsigned)XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP,
           (unsigned)XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME,
           (unsigned)XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS);

    for (;;)
    {
        radar_frame_result_t res;
        radar_gesture_t g;

        if (0U == ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RADAR_FRAME_TIMEOUT_MS)))
        {
            /* no frame interrupt: restart the sensor */
            taskENTER_CRITICAL();
            radar_state.fifo_errors++;
            taskEXIT_CRITICAL();
            (void)radar_restart_frames();
            continue;
        }

        if (XENSIV_BGT60TRXX_STATUS_OK !=
            xensiv_bgt60trxx_get_fifo_data(&bgt60_obj.dev, radar_frame,
                                           XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_FRAME))
        {
            /* FIFO overflow (GSR0 FOU_ERR) or SPI error: drop and restart */
            taskENTER_CRITICAL();
            radar_state.fifo_errors++;
            taskEXIT_CRITICAL();
            (void)radar_restart_frames();
            continue;
        }

        radar_dsp_process(&dsp, radar_frame, radar_gesture_clutter_mode(&gesture_ctx), &res);
        g = radar_gesture_update(&gesture_ctx, &res);

        taskENTER_CRITICAL();
        radar_state.frame = res;
        radar_state.frames++;
        if (RADAR_GESTURE_NONE != g)
        {
            radar_state.gestures++;
            radar_state.last_gesture = g;
            radar_state.last_d_az = gesture_ctx.last_d_az;
            radar_state.last_d_el = gesture_ctx.last_d_el;
            radar_state.last_d_rng = gesture_ctx.last_d_rng;
        }
        taskEXIT_CRITICAL();

        if (RADAR_GESTURE_NONE != g)
        {
            (void)xQueueSend(gesture_queue, &g, 0);
            radar_port_on_gesture(g);
            log_gesture(g, &gesture_ctx);
        }
    }
}
#endif /* RADAR_HW_AVAILABLE */


/*******************************************************************************
* Function Name: radar_task_start
*******************************************************************************/
cy_rslt_t radar_task_start(UBaseType_t priority)
{
    (void)memset(&radar_state, 0, sizeof(radar_state));

#if RADAR_HW_AVAILABLE
    gesture_queue = xQueueCreate(RADAR_GESTURE_QUEUE_LEN, sizeof(radar_gesture_t));
    if (NULL == gesture_queue)
    {
        radar_state.status = RADAR_STATUS_ERROR;
        return RADAR_RSLT_TASK_FAILED;
    }

    radar_state.status = RADAR_STATUS_INIT;
    if (pdPASS != xTaskCreate(radar_task, RADAR_TASK_NAME, RADAR_TASK_STACK_SIZE,
                              NULL, priority, &radar_task_handle))
    {
        radar_state.status = RADAR_STATUS_ERROR;
        return RADAR_RSLT_TASK_FAILED;
    }
    return CY_RSLT_SUCCESS;
#else
    (void)priority;
    radar_state.status = RADAR_STATUS_NOT_AVAILABLE;
    printf("Radar: this BSP has no BGT60TR13C (build with TARGET=KIT_PSE84_AI)\r\n");
    return RADAR_RSLT_NOT_AVAILABLE;
#endif
}

/* [] END OF FILE */
