/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for
*                    application running on CM55 CPU.
*
* Related Document : See README.md
*
********************************************************************************
* (c) 2025-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "cy_graphics.h"

#include "cy_time.h"

#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"

#include "retarget_io_init.h"

#include "mtb_disp_dsi_waveshare_4p3.h"

#include "infineon_logo.h"

#include "rtc_clock.h"
#include "rtc_console.h"
#include "rtc_hud.h"


/*******************************************************************************
* Macros
*******************************************************************************/
#define GFX_TASK_NAME                       ("CM55 Gfx Task")
/* stack size in words */
/* Increased from 3x to 5x for snprintf used by the RTC clock panel */
#define GFX_TASK_STACK_SIZE                 (configMINIMAL_STACK_SIZE * 5)

#define GFX_TASK_PRIORITY                   (configMAX_PRIORITIES - 1)

#define INITIAL_BTN_DEBOUNCE_DELAY          (500U)
#define BTN_DEBOUNCE_DELAY_MS               (10U)

#define DSI_ULPM_TIME_INTERVAL_MS           (6000U)

#define SET_VAL                             (1U)

#define GPIO_HIGH                           (1U)
#define GPIO_LOW                            (0U)
#define USER_BTN1_IRQ_PRIORITY              (3U)

#define I2C_CONTROLLER_IRQ_PRIORITY         (2UL)

/* Enabling or disabling a MCWDT requires a wait time of upto 2 CLK_LF cycles
 * to come into effect. This wait time value will depend on the actual CLK_LF
 * frequency set by the BSP.
 */
#define LPTIMER_1_WAIT_TIME_USEC            (62U)

/* Define the LPTimer interrupt priority number. '1' implies highest priority.
 */
#define APP_LPTIMER_INTERRUPT_PRIORITY      (1U)

/* RTC clock panel: position on the 800 x 480 panel (right side, middle band;
 * the other sensor demos use the top-left, top-right, left-middle and bottom
 * areas), UART console task priority (below the graphics task). */
#define RTC_HUD_X                           (448U)
#define RTC_HUD_Y                           (196U)
#define RTC_CONSOLE_PRIORITY                (configMAX_PRIORITIES - 2)

/* Idle wait of the GFX task between checks. Without it the highest priority
 * GFX task spins and starves the RTC console task. */
#define GFX_TASK_POLL_DELAY_MS              (20U)

/* Low-power clock mode ("L 1" on the UART): the DSI data lane stays in ULPM
 * and the CPU sleeps until the RTC minute alarm; each minute the panel is
 * redrawn and shown for RTC_LP_GLANCE_MS. */
#define RTC_LP_GLANCE_MS                    (3000U)


/*******************************************************************************
* Global Variables
*******************************************************************************/
GFXSS_Type* base = (GFXSS_Type*) GFXSS;

cy_stc_gfx_context_t gfx_context;

volatile bool user_button_flag = false;

/* true while the DSI data lane is in ULPM: the clock panel is not redrawn */
static bool dsi_in_ulpm = false;

/* Low-power clock mode requested from the UART console */
static volatile bool rtc_lp_request = false;

CY_SECTION(".cy_gpu_buf") uint8_t graphics_buffer[IMAGE_SIZE] = {0xFF};

/* USER BTN1 interrupt configuration */
const cy_stc_sysint_t user_btn1_irq_cfg =
{
    .intrSrc      = CYBSP_SW2_IRQ,
    .intrPriority = USER_BTN1_IRQ_PRIORITY
};

cy_stc_scb_i2c_context_t disp_i2c_controller_context;

cy_stc_sysint_t disp_i2c_controller_irq_cfg =
{
    .intrSrc      = CYBSP_I2C_CONTROLLER_IRQ,
    .intrPriority = I2C_CONTROLLER_IRQ_PRIORITY,
};

/* LPTimer HAL object */
static mtb_hal_lptimer_t lptimer_obj;

/* RTC HAL object */
static mtb_hal_rtc_t rtc_obj;


/*******************************************************************************
* Function Name: setup_clib_support
********************************************************************************
* Summary:
*    1. This function configures and initializes the Real-Time Clock (RTC)).
*    2. It then initializes the RTC HAL object to enable CLIB support library 
*       to work with the provided Real-Time Clock (RTC) module.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_clib_support(void)
{
    /* RTC Initialization is done in CM33 non-secure project */

    /* Initialize the ModusToolbox CLIB support library */
    mtb_clib_support_init(&rtc_obj);
}


/*******************************************************************************
* Function Name: lptimer_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for LPTimer instance.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}


/*******************************************************************************
* Function Name: setup_tickless_idle_timer
********************************************************************************
* Summary:
* 1. This function first configures and initializes an interrupt for LPTimer.
* 2. Then it initializes the LPTimer HAL object to be used in the RTOS
*    tickless idle mode implementation to allow the device enter deep sleep
*    when idle task runs. LPTIMER_1 instance is configured for CM55 CPU.
* 3. It then passes the LPTimer object to abstraction RTOS library that
*    implements tickless idle mode
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_tickless_idle_timer(void)
{
    /* Interrupt configuration structure for LPTimer */
    cy_stc_sysint_t lptimer_intr_cfg =
    {
        .intrSrc = CYBSP_CM55_LPTIMER_1_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    /* Initialize the LPTimer interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status =
                                    Cy_SysInt_Init(&lptimer_intr_cfg,
                                                    lptimer_interrupt_handler);

    /* LPTimer interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    /* Initialize the MCWDT block */
    cy_en_mcwdt_status_t mcwdt_init_status =
                                    Cy_MCWDT_Init(CYBSP_CM55_LPTIMER_1_HW,
                                                &CYBSP_CM55_LPTIMER_1_config);

    /* MCWDT initialization failed. Stop program execution. */
    if(CY_MCWDT_SUCCESS != mcwdt_init_status)
    {
        handle_app_error();
    }

    /* Enable MCWDT instance */
    Cy_MCWDT_Enable(CYBSP_CM55_LPTIMER_1_HW,
                    CY_MCWDT_CTR_Msk,
                    LPTIMER_1_WAIT_TIME_USEC);

    /* Setup LPTimer using the HAL object and desired configuration as defined
     * in the device configurator. */
    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj,
                                            &CYBSP_CM55_LPTIMER_1_hal_config);

    /* LPTimer setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Pass the LPTimer object to abstraction RTOS library that implements
     * tickless idle mode
     */
    cyabs_rtos_set_lptimer(&lptimer_obj);
}


/*******************************************************************************
* Function Name: disp_i2c_controller_interrupt
********************************************************************************
* Summary:
*  I2C controller ISR which invokes Cy_SCB_I2C_Interrupt to perform I2C transfer
*  as controller.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void disp_i2c_controller_interrupt(void)
{
    Cy_SCB_I2C_Interrupt(CYBSP_I2C_CONTROLLER_HW, &disp_i2c_controller_context);
}


/*******************************************************************************
* Function Name: button_isr_handler
********************************************************************************
* Summary:
*   Button interrupt handler to set user_button_flag.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
static void button_isr_handler(void)
{
    /* Check if USER_BTN1 was pressed */
    if(1UL == Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN))
    {
        user_button_flag = true;
        /* Clear the USER_BTN1 interrupt */
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN);
        NVIC_ClearPendingIRQ(CYBSP_USER_BTN_IRQ);
    }

    /* USER_BTN2 pin is present on the same GPIO port as USER_BTN1 pin */
    /* This cause the ISR to execute even when USER_BTN2 is pressed accidentally */
    /* To handle such scenarios we clear the interrupt */
    
    /* Check if USER_BTN2 was pressed */
    if(1UL == Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN))
    {
        /* Clear the USER_BTN2 interrupt */
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
        NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
    }
}


/*******************************************************************************
* Function Name: rtc_hud_update
********************************************************************************
* Summary:
*   Draws the clock panel (local time, date, time zone, sync source) into the
*   frame buffer and flushes the D-cache for the panel lines.
*
* Parameters:
*  st        : clock snapshot from rtc_clock_get_state()
*  low_power : true in the low-power clock mode (seconds hidden)
*
* Return:
*  void
*
*******************************************************************************/
static void rtc_hud_update(const rtc_clock_state_t *st, bool low_power)
{
    rtc_hud_view_t view;

    view.local      = st->local;
    view.tz_min     = st->tz_min;
    view.time_valid = st->time_valid;
    view.wco_ok     = st->wco_ok && st->bak_is_wco;
    view.low_power  = low_power;
    view.src_name   = rtc_clock_src_name(st->src);
    view.sync_age_s = st->sync_age_s;

    rtc_hud_draw((uint16_t*)graphics_buffer, IMAGE_WIDTH, IMAGE_HEIGHT,
                 RTC_HUD_X, RTC_HUD_Y, &view);

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    /* Make the CPU writes visible to the display controller (whole lines) */
    SCB_CleanDCache_by_Addr((volatile void*)&graphics_buffer[RTC_HUD_Y * IMAGE_WIDTH * COLOR_DEPTH],
                            (int32_t)(RTC_HUD_HEIGHT * IMAGE_WIDTH * COLOR_DEPTH));
#endif
}


/*******************************************************************************
* Function Name: rtc_lp_cb
********************************************************************************
* Summary:
*   UART console callback for "L 1" / "L 0" (runs in the console task).
*
*******************************************************************************/
static void rtc_lp_cb(bool enable)
{
    rtc_lp_request = enable;
    printf("RTC: low-power clock mode %s\r\n", enable ? "ON (display off, "
           "wake every minute)" : "OFF");
    /* leave rtc_clock_wait_minute() at once */
    rtc_clock_wake_waiter();
}


/*******************************************************************************
* Function Name: rtc_low_power_clock
********************************************************************************
* Summary:
*   Low-power clock mode. The DSI data lane goes to ULPM, the task blocks on
*   the RTC ALARM1 (second 00) so FreeRTOS tickless idle puts the CM55 into
*   Deep Sleep. Each minute: exit ULPM, redraw, show for RTC_LP_GLANCE_MS,
*   enter ULPM again. Returns when "L 0" is received.
*
*******************************************************************************/
static void rtc_low_power_clock(void)
{
    rtc_clock_state_t st;
    cy_en_mipidsi_status_t mipi_status;

    /* drop a stale alarm / wake so the first wait really sleeps */
    (void)rtc_clock_wait_minute(0U);

    while (rtc_lp_request)
    {
        rtc_clock_get_state(&st);
        rtc_hud_update(&st, true);

        mipi_status = Cy_MIPIDSI_EnterULPM_Data(GFXSS_GFXSS_MIPIDSI);
        if (CY_MIPIDSI_SUCCESS != mipi_status)
        {
            printf("[%s: %d] ULPM entry failed: %u\r\n", __func__, __LINE__, mipi_status);
            handle_app_error();
        }
        dsi_in_ulpm = true;
        Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN, GPIO_HIGH);

        /* CPU sleeps here (Deep Sleep when the UART console window is closed) */
        (void)rtc_clock_wait_minute(RTC_WAIT_FOREVER);

        mipi_status = Cy_MIPIDSI_ExitULPM_Data(GFXSS_GFXSS_MIPIDSI);
        if (CY_MIPIDSI_SUCCESS != mipi_status)
        {
            printf("[%s: %d] ULPM exit failed: %u\r\n", __func__, __LINE__, mipi_status);
            handle_app_error();
        }
        dsi_in_ulpm = false;
        Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN, GPIO_LOW);

        rtc_clock_get_state(&st);
        rtc_hud_update(&st, rtc_lp_request);
        rtc_console_print_status();

        if (rtc_lp_request)
        {
            /* glance: show the new minute, keep the UART console open */
            rtc_console_open_window(RTC_LP_GLANCE_MS + 2000U);
            vTaskDelay(pdMS_TO_TICKS(RTC_LP_GLANCE_MS));
        }
    }
}


/*******************************************************************************
* Function Name: cm55_ns_gfx_task
********************************************************************************
* Summary:
*   This is the FreeRTOS task callback function.
*   It initializes:
*       - GFX subsystem.
*       - Initializes "Waveshare 4.3-inch DSI LCD panel".
*       - Display the Infineon logo and based on USER BTN1 press, exercises
*         MIPI DSI ULPM.
*
* Parameters:
*  void *arg (unused)
*
* Return:
*  void
*
*******************************************************************************/
static void cm55_ns_gfx_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);

    GFXSS_config.mipi_dsi_cfg = &mtb_disp_waveshare_4p3_dsi_config;
    cy_en_scb_i2c_status_t i2c_result = CY_SCB_I2C_SUCCESS;
    cy_rslt_t panel_status = CY_RSLT_SUCCESS;
    volatile cy_en_gfx_status_t status;
    volatile cy_en_mipidsi_status_t mipi_status;
    cy_en_sysint_status_t sysint_status = CY_SYSINT_SUCCESS;
    rtc_clock_state_t rtc_st;
    uint8_t last_sec = 0xFFU;
    uint8_t last_min = 0xFFU;

    /* Initial delay to allow debounce delays to settle */
    Cy_SysLib_Delay(INITIAL_BTN_DEBOUNCE_DELAY);

    /* CYBSP_USER_BTN1 (SW2) and CYBSP_USER_BTN2 (SW4) share the same port in the
    * PSOC™ Edge E84 evaluation kit and hence they share the same NVIC IRQ line.
    * Since both are configured in the BSP via the Device Configurator, the
    * interrupt flags for both the buttons are set right after they get initialized
    * through the call to cybsp_init(). The flags must be cleared before initializing
    * the interrupt, otherwise the interrupt line will be constantly asserted.
    */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);

    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);

    /* Setting graphics frame buffers address to gfx_buff address */
    GFXSS_config.dc_cfg->gfx_layer_config->buffer_address = (size_t* )&graphics_buffer;
    GFXSS_config.dc_cfg->gfx_layer_config->uv_buffer_address = (size_t* )&graphics_buffer;
    
    /* Initializes the graphics subsystem according to the configuration */
    status = Cy_GFXSS_Init(base, &GFXSS_config, &gfx_context);

    /* Graphics subsystem initialization failed. Stop program execution */
    if (CY_GFX_SUCCESS != status)
    {
        printf("[%s: %d] Gfxss initialization failed. Error type: %u\r\n", __func__, __LINE__, status);
        handle_app_error();
    }

    /* Initialize the I2C in controller mode. */
    i2c_result = Cy_SCB_I2C_Init(CYBSP_I2C_CONTROLLER_HW,
                &CYBSP_I2C_CONTROLLER_config, &disp_i2c_controller_context);

    if (CY_SCB_I2C_SUCCESS != i2c_result)
    {
        printf("I2C controller initialization failed !!\n");
        handle_app_error();
    }

    /* Initialize the I2C interrupt */
    sysint_status = Cy_SysInt_Init(&disp_i2c_controller_irq_cfg,
                                    &disp_i2c_controller_interrupt);

    if (CY_SYSINT_SUCCESS != sysint_status)
    {
        printf("I2C controller interrupt initialization failed\r\n");
        handle_app_error();
    }

    /* Enable the I2C interrupts. */
    NVIC_EnableIRQ(disp_i2c_controller_irq_cfg.intrSrc);

    /* Enable the I2C */
    Cy_SCB_I2C_Enable(CYBSP_I2C_CONTROLLER_HW);

    /* Initialize the display */
    panel_status = mtb_disp_waveshare_4p3_init(CYBSP_I2C_CONTROLLER_HW,
                                                &disp_i2c_controller_context);

    if (CY_RSLT_SUCCESS != panel_status)
    {
        printf("Waveshare 4.3-inch R-Pi display init failed with status = %u\r\n", (unsigned int) panel_status);
        handle_app_error();
    }

    memcpy(graphics_buffer, img_ptr, sizeof(img_ptr));

    /* RTC clock: WCO / backup clock check, minute alarm, UART console.
     * A failure is not fatal: the panel then shows the error state. */
    if (CY_RSLT_SUCCESS != rtc_clock_init())
    {
        printf("RTC clock init failed\r\n");
    }
    rtc_clock_get_state(&rtc_st);
    rtc_hud_update(&rtc_st, false);
    rtc_console_print_status();

    if (CY_RSLT_SUCCESS != rtc_console_start(RTC_CONSOLE_PRIORITY, rtc_lp_cb))
    {
        printf("RTC console start failed\r\n");
    }

    /* Set the frame buffer with the image pointer to be displayed on LCD */
    status = Cy_GFXSS_Set_FrameBuffer(base, (uint32_t*)graphics_buffer, &gfx_context);

    /* Image rendering failed. Stop program execution */
    if (CY_GFX_SUCCESS != status)
    {
        printf("[%s: %d] Image rendering failed. Error type: %u\r\n", __func__, __LINE__, status);
        handle_app_error();
    }

    /* Enable USER BTN1 interrupt */
    NVIC_EnableIRQ(user_btn1_irq_cfg.intrSrc);

    user_button_flag = false;

    printf("Press USER BTN1 button to enter ULPM\r\n\n");

    for (;;)
    {
        /* Low-power clock mode requested on the UART ("L 1") */
        if (rtc_lp_request)
        {
            rtc_low_power_clock();
            last_sec = 0xFFU;
        }

        /* Redraw the clock panel when the second changes; log once a minute */
        if (!dsi_in_ulpm)
        {
            rtc_clock_get_state(&rtc_st);
            if (rtc_st.local.sec != last_sec)
            {
                last_sec = rtc_st.local.sec;
                rtc_hud_update(&rtc_st, false);
                if (rtc_st.local.min != last_min)
                {
                    last_min = rtc_st.local.min;
                    rtc_console_print_status();
                }
            }
        }

        /* Check for the USER BTN1 interrupt status */
        if (user_button_flag)
        {
            while (CYBSP_BTN_PRESSED == Cy_GPIO_Read(CYBSP_USER_BTN_PORT, CYBSP_USER_BTN_PIN))
            {
                /* Wait for 10 millisecond */
                vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_DELAY_MS));
            }

            printf("Entering ULPM: Switching OFF LCD for 6 seconds\r\n\n");

            /* Enter ultra low power mode (data lane only integrated) */
            mipi_status = Cy_MIPIDSI_EnterULPM_Data(GFXSS_GFXSS_MIPIDSI);

            if (CY_MIPIDSI_SUCCESS != mipi_status)
            {
                printf("[%s: %d] Entry to Ultra Low Power Mode on data lane failed. Error type: %u\r\n", __func__, __LINE__, mipi_status);
                handle_app_error();
            }

            dsi_in_ulpm = true;
            Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN, GPIO_HIGH);

            vTaskDelay(pdMS_TO_TICKS(DSI_ULPM_TIME_INTERVAL_MS));

            /* Exit ultra low power mode (data lane only integrated) */
            mipi_status = Cy_MIPIDSI_ExitULPM_Data(GFXSS_GFXSS_MIPIDSI);
            if (CY_MIPIDSI_SUCCESS != mipi_status)
            {
                printf("[%s: %d] Exit from Ultra Low Power Mode on data lane failed. Error type: %u\r\n", __func__, __LINE__, mipi_status);
                handle_app_error();
            }

            dsi_in_ulpm = false;
            Cy_GPIO_Write(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN, GPIO_LOW);
            printf("Exited ULPM with SUCCESS: Press to enter again ULPM\r\n\n");

            /* Clear USER BTN1 interrupt flag */
            user_button_flag = false;
        }

        vTaskDelay(pdMS_TO_TICKS(GFX_TASK_POLL_DELAY_MS));
    }
}


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  This is the main function for CM55 non-secure application.
*    1. It initializes the device and board peripherals.
*    2. It creates the FreeRTOS application task 'cm55_ns_gfx_task'.
*    3. It starts the RTOS task scheduler.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    BaseType_t task_return;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Initialize retarget-io middleware */
    init_retarget_io();

    /* Initialize USER BTN1 interrupt */
    Cy_SysInt_Init(&user_btn1_irq_cfg, &button_isr_handler);

    /* Setup CLIB support library. */
    setup_clib_support();

    /* Setup the LPTimer instance for CM55*/
    setup_tickless_idle_timer();

    /* Enable global interrupts */
    __enable_irq();

    /* Create the FreeRTOS Task */
    task_return = xTaskCreate(cm55_ns_gfx_task, GFX_TASK_NAME,
                              GFX_TASK_STACK_SIZE, NULL,
                              GFX_TASK_PRIORITY, NULL);

    if (pdPASS == task_return)
    {
        /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
        printf("\x1b[2J\x1b[;H");

        printf("****************** "
                "PSOC Edge MCU: Graphics DSI ULPM On Data Lane "
                "****************** \r\n\n");

        /* Start the RTOS Scheduler */
        vTaskStartScheduler();

        /* Should never get here! */
        /* Halt the CPU if scheduler exits */
        handle_app_error();
    }
    else
    {
        printf("Error: Failed to create cm55_ns_gfx_task.\r\n");
        handle_app_error();
    }
}


/* [] END OF FILE */