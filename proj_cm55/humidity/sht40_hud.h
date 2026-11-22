/*******************************************************************************
* File Name        : sht40_hud.h
*
* Description      : Draws the SHT40 temperature / humidity panel into an
*                    RGB565 frame buffer (5x7 bitmap font, integer scaling).
*                    Hardware independent: the caller flushes the D-cache.
*
*******************************************************************************/
#ifndef SHT40_HUD_H
#define SHT40_HUD_H

#include <stdbool.h>
#include <stdint.h>

#include "sht40_math.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SHT40_HUD_RGB565(r, g, b)   ((uint16_t)((((r) & 0xF8U) << 8) | \
                                                (((g) & 0xFCU) << 3) | \
                                                ((b) >> 3)))

#define SHT40_HUD_SCALE             (3U)
#define SHT40_HUD_COLS              (16U)
#define SHT40_HUD_LINES             (5U)
#define SHT40_HUD_PAD               (8U)
#define SHT40_HUD_CHAR_ADV          (6U * SHT40_HUD_SCALE)
#define SHT40_HUD_LINE_ADV          (9U * SHT40_HUD_SCALE)
#define SHT40_HUD_WIDTH             ((SHT40_HUD_COLS * SHT40_HUD_CHAR_ADV) + (2U * SHT40_HUD_PAD))
#define SHT40_HUD_HEIGHT            ((SHT40_HUD_LINES * SHT40_HUD_LINE_ADV) + (2U * SHT40_HUD_PAD))

typedef enum
{
    SHT40_HUD_MODE_WAIT = 0,
    SHT40_HUD_MODE_DATA,
    SHT40_HUD_MODE_HEATING,
    SHT40_HUD_MODE_NO_SENSOR,
    SHT40_HUD_MODE_ERROR
} sht40_hud_mode_t;

typedef struct
{
    sht40_hud_mode_t  mode;
    float             temperature_c;
    float             humidity_pct;
    float             dew_point_c;
    sht40_fog_level_t fog_level;
    bool              batt_heater_on;
} sht40_hud_view_t;

/* Draws the panel with its top-left corner at (x, y).
 * fb: RGB565 frame buffer, stride: pixels per line, fb_height: lines.
 * The panel is SHT40_HUD_WIDTH x SHT40_HUD_HEIGHT pixels and is clipped to
 * the buffer. */
void sht40_hud_draw(uint16_t* fb, uint32_t stride, uint32_t fb_height,
                    uint32_t x, uint32_t y, const sht40_hud_view_t* view);

#if defined(__cplusplus)
}
#endif

#endif /* SHT40_HUD_H */
