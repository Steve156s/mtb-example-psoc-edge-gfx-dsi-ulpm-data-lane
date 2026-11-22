/*******************************************************************************
* File Name        : rtc_hud.h
*
* Description      : Draws the HUD clock panel (big 7-segment HH:MM, seconds,
*                    date, day of week, time zone and sync source) into an
*                    RGB565 frame buffer. Hardware independent: also built on
*                    the host by tools/rtc_host_test to render a preview.
*
*******************************************************************************/
#ifndef RTC_HUD_H
#define RTC_HUD_H

#include <stdbool.h>
#include <stdint.h>

#include "rtc_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Panel size in pixels */
#define RTC_HUD_WIDTH       (336U)
#define RTC_HUD_HEIGHT      (150U)

#define RTC_HUD_RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | (((uint16_t)(g) & 0xFCU) << 3) | \
                ((uint16_t)(b) >> 3)))

typedef struct
{
    rtc_datetime_t local;       /* local date / time to show              */
    int16_t        tz_min;      /* time zone, minutes east of UTC         */
    bool           time_valid;  /* false: RTC never set -> "SET TIME"     */
    bool           wco_ok;      /* false: WCO crystal not running         */
    bool           low_power;   /* low-power clock-only mode active       */
    const char    *src_name;    /* "GNSS", "PHONE", "UART", "NONE"        */
    uint32_t       sync_age_s;  /* seconds since the last sync            */
} rtc_hud_view_t;

/* Draws the panel with its top-left corner at (x, y).
 * fb points to an RGB565 buffer of stride x height pixels. */
void rtc_hud_draw(uint16_t *fb, uint32_t stride, uint32_t height,
                  uint32_t x, uint32_t y, const rtc_hud_view_t *view);

/* "45S", "12M", "3H", "2D" style age string; buf must hold 8 chars. */
char *rtc_hud_format_age(uint32_t age_s, char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* RTC_HUD_H */
