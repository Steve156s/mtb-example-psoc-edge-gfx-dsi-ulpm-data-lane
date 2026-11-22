/*******************************************************************************
* File Name        : hud_text.h
*
* Description      : Minimal RGB565 text renderer (5x7 bitmap font, integer
*                    scaling) that draws directly into the GFXSS frame buffer.
*                    Used to overlay barometer readings on the display.
*
*******************************************************************************/
#ifndef HUD_TEXT_H
#define HUD_TEXT_H

#include <stdint.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define HUD_RGB565(r, g, b)     ((uint16_t)((((r) & 0xF8U) << 8) | \
                                            (((g) & 0xFCU) << 3) | \
                                            ((b) >> 3)))

#define HUD_COLOR_BLACK         HUD_RGB565(0U,   0U,   0U)
#define HUD_COLOR_WHITE         HUD_RGB565(255U, 255U, 255U)
#define HUD_COLOR_GREEN         HUD_RGB565(0U,   255U, 120U)
#define HUD_COLOR_CYAN          HUD_RGB565(0U,   200U, 255U)
#define HUD_COLOR_RED           HUD_RGB565(255U, 40U,  40U)

#define HUD_FONT_W              (5U)
#define HUD_FONT_H              (7U)
#define HUD_CHAR_ADVANCE(scale) ((HUD_FONT_W + 1U) * (scale))
#define HUD_LINE_ADVANCE(scale) ((HUD_FONT_H + 2U) * (scale))

typedef struct
{
    uint16_t* pixels;   /* RGB565 frame buffer */
    uint32_t  width;    /* pixels per line (stride) */
    uint32_t  height;
} hud_surface_t;

void hud_fill_rect(const hud_surface_t* s, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint16_t color);
uint32_t hud_draw_text(const hud_surface_t* s, uint32_t x, uint32_t y,
                       const char* text, uint16_t fg, uint16_t bg,
                       uint32_t scale);
void hud_flush(const hud_surface_t* s, uint32_t x, uint32_t y,
               uint32_t w, uint32_t h);
size_t hud_format_fixed(char* buf, size_t buf_len, float value,
                        uint32_t decimals, int force_sign);

#if defined(__cplusplus)
}
#endif

#endif /* HUD_TEXT_H */

/* [] END OF FILE */
