/*******************************************************************************
* File Name        : hud_text.h
*
* Description      : Minimal RGB565 drawing helpers (filled rectangle and a
*                    scaled 5x7 bitmap font) for a simple sensor read-out on
*                    top of the frame buffer. No graphics library needed.
*
*                    Supported characters: space, 0-9, A-Z (lower case is
*                    drawn as upper case), and  - . : + / % = ! ( ) |
*
*******************************************************************************/
#ifndef HUD_TEXT_H
#define HUD_TEXT_H

#include <stdint.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define HUD_RGB565(r, g, b)  ((uint16_t)((((r) & 0xF8U) << 8) | \
                                         (((g) & 0xFCU) << 3) | \
                                         (((b) & 0xF8U) >> 3)))

#define HUD_COLOR_BLACK      HUD_RGB565(0x00, 0x00, 0x00)
#define HUD_COLOR_WHITE      HUD_RGB565(0xFF, 0xFF, 0xFF)
#define HUD_COLOR_GREEN      HUD_RGB565(0x00, 0xE0, 0x60)
#define HUD_COLOR_YELLOW     HUD_RGB565(0xFF, 0xD0, 0x00)
#define HUD_COLOR_RED        HUD_RGB565(0xFF, 0x20, 0x20)
#define HUD_COLOR_GREY       HUD_RGB565(0x90, 0x90, 0x90)
#define HUD_COLOR_PANEL      HUD_RGB565(0x10, 0x10, 0x18)

#define HUD_FONT_W           (5U)
#define HUD_FONT_H           (7U)

typedef struct
{
    uint16_t *pixels;     /* RGB565 frame buffer                    */
    uint32_t  stride;     /* pixels per line (may exceed width)     */
    uint32_t  width;      /* visible width                          */
    uint32_t  height;     /* visible height                         */
} hud_surface_t;

void hud_fill_rect(const hud_surface_t *s, int32_t x, int32_t y,
                   int32_t w, int32_t h, uint16_t color);

/* Draws text with each font pixel scaled to scale x scale. Background pixels
 * of each glyph cell are filled with bg so old text is overwritten.
 * Returns the x position after the last character. */
int32_t hud_draw_text(const hud_surface_t *s, int32_t x, int32_t y,
                      const char *text, uint32_t scale,
                      uint16_t fg, uint16_t bg);

/* Formats value with one decimal into buf ("-12.3"), right-aligned to width
 * characters (pads with spaces). Avoids printf float support. */
void hud_format_fixed1(char *buf, size_t buf_len, float value, uint32_t width);

#if defined(__cplusplus)
}
#endif

#endif /* HUD_TEXT_H */
