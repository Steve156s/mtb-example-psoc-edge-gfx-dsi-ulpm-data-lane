/*******************************************************************************
* File Name        : rtc_console.c
*
* Description      : UART time-set console for the HUD clock. See rtc_console.h.
*
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "cybsp.h"
#include "cy_scb_uart.h"
#include "cy_syspm.h"

#include "FreeRTOS.h"
#include "task.h"

#include "rtc_clock.h"
#include "rtc_console.h"
#include "rtc_time.h"

#define CONSOLE_TASK_NAME       ("RTC Console")
#define CONSOLE_STACK_WORDS     (configMINIMAL_STACK_SIZE * 4)
#define CONSOLE_LINE_MAX        (48U)

static rtc_console_lp_cb_t lp_callback;
static volatile bool       lp_mode;
static volatile TickType_t awake_until;


/*******************************************************************************
* Deep Sleep policy: the SCB UART RX FIFO is not clocked in Deep Sleep, so
* refuse Deep Sleep while the console is awake. Sleep is still used.
*******************************************************************************/
#if (RTC_CONSOLE_BLOCK_DEEPSLEEP != 0) && \
    defined(CY_CFG_PWR_SYS_IDLE_MODE) && (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
static bool console_awake(void)
{
    if (!lp_mode)
    {
        return true;
    }
    return ((int32_t)(awake_until - xTaskGetTickCount()) > 0);
}

static cy_en_syspm_status_t console_deepsleep_cb(cy_stc_syspm_callback_params_t *params,
                                                 cy_en_syspm_callback_mode_t mode)
{
    CY_UNUSED_PARAMETER(params);

    if ((CY_SYSPM_CHECK_READY == mode) && console_awake())
    {
        return CY_SYSPM_FAIL;
    }
    return CY_SYSPM_SUCCESS;
}

static cy_stc_syspm_callback_params_t console_cb_params = { NULL, NULL };

static cy_stc_syspm_callback_t console_cb =
{
    .callback       = &console_deepsleep_cb,
    .type           = CY_SYSPM_DEEPSLEEP,
    .skipMode       = 0U,
    .callbackParams = &console_cb_params,
    .prevItm        = NULL,
    .nextItm        = NULL,
    .order          = 2U
};
#define CONSOLE_HAS_DS_CB   (1)
#endif


static void print_help(void)
{
    printf("RTC commands:\r\n"
           "  T YYYY-MM-DD HH:MM[:SS]  set local time\r\n"
           "  U <unix seconds>         set UTC (GNSS / phone format)\r\n"
           "  Z +8 | Z -3:30           set time zone\r\n"
           "  C +2.5                   crystal correction (ppm, + = slow)\r\n"
           "  L 1 | L 0                low-power clock mode on / off\r\n"
           "  S                        status\r\n");
}


void rtc_console_print_status(void)
{
    rtc_clock_state_t st;
    char d[12], t[10], z[12];

    rtc_clock_get_state(&st);
    printf("RTC: %s %s %s %s | UTC %lu | src %s, age %lu s | WCO %s | "
           "calib %d.%d ppm | min ticks %lu%s\r\n",
           rtc_time_format_date(&st.local, d, sizeof(d)),
           rtc_time_format_time(&st.local, t, sizeof(t)),
           rtc_time_wday_name(st.local.wday),
           rtc_time_format_tz(st.tz_min, z, sizeof(z)),
           (unsigned long)st.unix_utc, rtc_clock_src_name(st.src),
           (unsigned long)st.sync_age_s, st.wco_ok ? "OK" : "FAIL",
           st.calib_ppm_x10 / 10, (st.calib_ppm_x10 < 0 ? -st.calib_ppm_x10 : st.calib_ppm_x10) % 10,
           (unsigned long)st.minute_ticks,
           st.time_valid ? "" : " | NOT SET (use T command)");
}


static void handle_line(const char *line)
{
    rtc_cmd_t cmd;

    switch (rtc_time_parse_command(line, &cmd))
    {
        case RTC_CMD_NONE:
            break;

        case RTC_CMD_SET_LOCAL:
            (void)rtc_clock_set_local(RTC_SRC_UART, &cmd.local);
            rtc_console_print_status();
            break;

        case RTC_CMD_SET_UTC:
            (void)rtc_clock_sync(RTC_SRC_UART, cmd.unix_utc);
            rtc_console_print_status();
            break;

        case RTC_CMD_SET_TZ:
            rtc_clock_set_tz(cmd.tz_min);
            rtc_console_print_status();
            break;

        case RTC_CMD_SET_CALIB:
            printf("RTC: calibration %s\r\n",
                   rtc_clock_set_calibration(cmd.ppm_x10) ? "applied" : "FAILED");
            break;

        case RTC_CMD_LOW_POWER:
            rtc_console_set_low_power(cmd.enable);
            if (NULL != lp_callback)
            {
                lp_callback(cmd.enable);
            }
            break;

        case RTC_CMD_STATUS:
            rtc_console_print_status();
            break;

        case RTC_CMD_HELP:
            print_help();
            break;

        case RTC_CMD_ERROR:
        default:
            printf("RTC: bad command '%s' (H for help)\r\n", line);
            break;
    }
}


static void console_task(void *arg)
{
    char line[CONSOLE_LINE_MAX];
    uint32_t len = 0U;

    CY_UNUSED_PARAMETER(arg);
    print_help();

    for (;;)
    {
        uint32_t data;

        while (CY_SCB_UART_RX_NO_DATA != (data = Cy_SCB_UART_Get(CYBSP_DEBUG_UART_HW)))
        {
            char ch = (char)(data & 0xFFU);

            rtc_console_open_window(RTC_CONSOLE_WINDOW_MS);

            if (('\r' == ch) || ('\n' == ch))
            {
                if (len > 0U)
                {
                    line[len] = '\0';
                    printf("\r\n");
                    handle_line(line);
                    len = 0U;
                }
            }
            else if ((('\b' == ch) || (0x7F == ch)) && (len > 0U))
            {
                len--;
                printf("\b \b");
            }
            else if ((ch >= ' ') && (len < (CONSOLE_LINE_MAX - 1U)))
            {
                line[len++] = ch;
                (void)putchar(ch);         /* local echo */
            }
            else
            {
                /* ignore other control characters / overflow */
            }
        }
        (void)fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(RTC_CONSOLE_POLL_MS));
    }
}


cy_rslt_t rtc_console_start(uint32_t priority, rtc_console_lp_cb_t lp_cb)
{
    lp_callback = lp_cb;
    lp_mode     = false;
    awake_until = xTaskGetTickCount();

#if defined(CONSOLE_HAS_DS_CB)
    if (!Cy_SysPm_RegisterCallback(&console_cb))
    {
        return CY_RSLT_TYPE_ERROR;
    }
#endif

    return (pdPASS == xTaskCreate(console_task, CONSOLE_TASK_NAME, CONSOLE_STACK_WORDS,
                                  NULL, priority, NULL)) ?
           CY_RSLT_SUCCESS : CY_RSLT_TYPE_ERROR;
}


void rtc_console_set_low_power(bool enable)
{
    lp_mode = enable;
    rtc_console_open_window(RTC_CONSOLE_WINDOW_MS);
}


void rtc_console_open_window(uint32_t ms)
{
    awake_until = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
}
