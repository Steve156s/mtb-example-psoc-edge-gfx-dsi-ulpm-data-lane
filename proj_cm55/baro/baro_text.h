/*******************************************************************************
* File Name        : baro_text.h
*
* Description      : Minimal RGB565 text renderer (5x7 bitmap font, integer
*                    scaling) that draws directly into the GFXSS frame buffer.
*                    Used to overlay barometer readings on the display.
*
*******************************************************************************/
#ifndef BARO_TEXT_H
#define BARO_TEXT_H

#include <stdint.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define BARO_TXT_RGB565(r, g, b)     ((uint16_t)((((r) & 0xF8U) << 8) | \
                                            (((g) & 0xFCU) << 3) | \
                                            ((b) >> 3)))

#define BARO_TXT_COLOR_BLACK         BARO_TXT_RGB565(0U,   0U,   0U)
#define BARO_TXT_COLOR_WHITE         BARO_TXT_RGB565(255U, 255U, 255U)
#define BARO_TXT_COLOR_GREEN         BARO_TXT_RGB565(0U,   255U, 120U)
#define BARO_TXT_COLOR_CYAN          BARO_TXT_RGB565(0U,   200U, 255U)
#define BARO_TXT_COLOR_RED           BARO_TXT_RGB565(255U, 40U,  40U)

#define BARO_TXT_FONT_W              (5U)
#define BARO_TXT_FONT_H              (7U)
#define BARO_TXT_CHAR_ADVANCE(scale) ((BARO_TXT_FONT_W + 1U) * (scale))
#define BARO_TXT_LINE_ADVANCE(scale) ((BARO_TXT_FONT_H + 2U) * (scale))

typedef struct
{
    uint16_t* pixels;   /* RGB565 frame buffer */
    uint32_t  width;    /* pixels per line (stride) */
    uint32_t  height;
} baro_txt_surface_t;

void baro_txt_fill_rect(const baro_txt_surface_t* s, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint16_t color);
uint32_t baro_txt_draw_text(const baro_txt_surface_t* s, uint32_t x, uint32_t y,
                       const char* text, uint16_t fg, uint16_t bg,
                       uint32_t scale);
void baro_txt_flush(const baro_txt_surface_t* s, uint32_t x, uint32_t y,
               uint32_t w, uint32_t h);
size_t baro_txt_format_fixed(char* buf, size_t buf_len, float value,
                        uint32_t decimals, int force_sign);

#if defined(__cplusplus)
}
#endif

#endif /* BARO_TEXT_H */

/* [] END OF FILE */
