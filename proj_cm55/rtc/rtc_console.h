/*******************************************************************************
* File Name        : rtc_console.h
*
* Description      : UART time-set console for the HUD clock (debug UART of
*                    the CM55, KitProg3 USB-UART, 115200 8N1).
*
*                    T 2026-11-22 10:30:00   set local time (seconds optional)
*                    U 1795341909            set UTC as Unix time (as from GNSS)
*                    Z +8 | Z -3:30          set time zone
*                    C +2.5                  crystal correction in ppm
*                    L 1 | L 0               low-power clock mode on / off
*                    S or ?                  status, H help
*
*                    The SCB UART cannot receive in Deep Sleep. While the
*                    console is "awake" a SysPm callback refuses Deep Sleep
*                    (the CPU still uses Sleep). In low-power clock mode the
*                    console is awake only for RTC_CONSOLE_WINDOW_MS after
*                    each minute alarm or received character.
*
*******************************************************************************/
#ifndef RTC_CONSOLE_H
#define RTC_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#include "cy_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UART polling period in ms */
#ifndef RTC_CONSOLE_POLL_MS
#define RTC_CONSOLE_POLL_MS             (20U)
#endif

/* Awake window in low-power clock mode */
#ifndef RTC_CONSOLE_WINDOW_MS
#define RTC_CONSOLE_WINDOW_MS           (5000U)
#endif

/* 1: block Deep Sleep while the console is awake (needed for UART RX).
 * 0: never block (UART commands may lose characters in Deep Sleep). */
#ifndef RTC_CONSOLE_BLOCK_DEEPSLEEP
#define RTC_CONSOLE_BLOCK_DEEPSLEEP     (1)
#endif

/* Called from the console task when "L 1" / "L 0" is received. */
typedef void (*rtc_console_lp_cb_t)(bool enable);

/* Creates the console task and registers the Deep Sleep callback. */
cy_rslt_t rtc_console_start(uint32_t priority, rtc_console_lp_cb_t lp_cb);

/* Low-power clock mode: the console then stays awake only in windows. */
void rtc_console_set_low_power(bool enable);

/* Keeps the console (and the CPU out of Deep Sleep) awake for ms. */
void rtc_console_open_window(uint32_t ms);

/* Prints the clock status line on the UART. */
void rtc_console_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* RTC_CONSOLE_H */
