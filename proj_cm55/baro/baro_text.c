/*******************************************************************************
* File Name        : baro_text.c
*
* Description      : Minimal RGB565 text renderer (5x7 bitmap font) for the
*                    barometer overlay. Only the glyphs needed by the HUD are
*                    included: digits, a few upper/lower case letters and
*                    punctuation. Unknown characters render as blank.
*
*******************************************************************************/
#include <string.h>

#include "cy_pdl.h"
#include "baro_text.h"

typedef struct
{
    char    ch;
    uint8_t rows[BARO_TXT_FONT_H];   /* bit4 = leftmost pixel */
} baro_txt_glyph_t;

static const baro_txt_glyph_t baro_txt_font[] =
{
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
    { '*', { 0x0C, 0x12, 0x12, 0x0C, 0x00, 0x00, 0x00 } },  /* degree sign */
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'a', { 0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F } },
    { 'h', { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11 } },
    { 'm', { 0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11 } },
};

static const uint8_t* baro_txt_find_glyph(char ch)
{
    for (size_t i = 0U; i < (sizeof(baro_txt_font) / sizeof(baro_txt_font[0])); i++)
    {
        if (baro_txt_font[i].ch == ch)
        {
            return baro_txt_font[i].rows;
        }
    }
    return NULL;
}


void baro_txt_fill_rect(const baro_txt_surface_t* s, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint16_t color)
{
    if ((x >= s->width) || (y >= s->height))
    {
        return;
    }
    if ((x + w) > s->width)
    {
        w = s->width - x;
    }
    if ((y + h) > s->height)
    {
        h = s->height - y;
    }

    for (uint32_t row = y; row < (y + h); row++)
    {
        uint16_t* p = &s->pixels[(row * s->width) + x];
        for (uint32_t col = 0U; col < w; col++)
        {
            p[col] = color;
        }
    }
}


/* Returns the x coordinate after the last drawn character */
uint32_t baro_txt_draw_text(const baro_txt_surface_t* s, uint32_t x, uint32_t y,
                       const char* text, uint16_t fg, uint16_t bg,
                       uint32_t scale)
{
    if (0U == scale)
    {
        scale = 1U;
    }

    for (; '\0' != *text; text++)
    {
        const uint8_t* rows = baro_txt_find_glyph(*text);

        /* Background of the full character cell incl. 1 column spacing */
        baro_txt_fill_rect(s, x, y, BARO_TXT_CHAR_ADVANCE(scale), BARO_TXT_FONT_H * scale, bg);

        if (NULL != rows)
        {
            for (uint32_t r = 0U; r < BARO_TXT_FONT_H; r++)
            {
                for (uint32_t c = 0U; c < BARO_TXT_FONT_W; c++)
                {
                    if (0U != (rows[r] & (0x10U >> c)))
                    {
                        baro_txt_fill_rect(s, x + (c * scale), y + (r * scale),
                                      scale, scale, fg);
                    }
                }
            }
        }

        x += BARO_TXT_CHAR_ADVANCE(scale);
    }

    return x;
}


/* Writes back the D-cache for the given region so the display controller
 * (bus master) sees the new pixels. */
void baro_txt_flush(const baro_txt_surface_t* s, uint32_t x, uint32_t y,
               uint32_t w, uint32_t h)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((y >= s->height) || (0U == h))
    {
        return;
    }
    if ((y + h) > s->height)
    {
        h = s->height - y;
    }
    CY_UNUSED_PARAMETER(x);
    CY_UNUSED_PARAMETER(w);

    /* Flush whole lines: simpler and keeps the address range contiguous */
    uint32_t start = (uint32_t)&s->pixels[y * s->width];
    uint32_t end   = (uint32_t)&s->pixels[(y + h) * s->width];
    start &= ~(uint32_t)(__SCB_DCACHE_LINE_SIZE - 1U);

    SCB_CleanDCache_by_Addr((void*)start, (int32_t)(end - start));
#else
    CY_UNUSED_PARAMETER(s);
    CY_UNUSED_PARAMETER(x);
    CY_UNUSED_PARAMETER(y);
    CY_UNUSED_PARAMETER(w);
    CY_UNUSED_PARAMETER(h);
#endif
}


/* Formats a float as fixed point without relying on printf float support
 * (newlib-nano). Returns the string length. */
size_t baro_txt_format_fixed(char* buf, size_t buf_len, float value,
                        uint32_t decimals, int force_sign)
{
    static const uint32_t pow10[] = { 1U, 10U, 100U, 1000U };
    char tmp[16];
    size_t n = 0U;
    size_t len = 0U;
    int negative = (value < 0.0f);

    if ((NULL == buf) || (0U == buf_len))
    {
        return 0U;
    }
    if (decimals > 3U)
    {
        decimals = 3U;
    }

    float mag = negative ? -value : value;
    uint32_t scaled = (uint32_t)((mag * (float)pow10[decimals]) + 0.5f);
    uint32_t ipart = scaled / pow10[decimals];
    uint32_t fpart = scaled % pow10[decimals];

    if (0U == scaled)
    {
        negative = 0;   /* avoid "-0.0" */
    }

    /* Build reversed */
    for (uint32_t d = 0U; d < decimals; d++)
    {
        tmp[n++] = (char)('0' + (fpart % 10U));
        fpart /= 10U;
    }
    if (decimals > 0U)
    {
        tmp[n++] = '.';
    }
    do
    {
        tmp[n++] = (char)('0' + (ipart % 10U));
        ipart /= 10U;
    } while ((0U != ipart) && (n < (sizeof(tmp) - 1U)));

    if (negative)
    {
        tmp[n++] = '-';
    }
    else if (force_sign)
    {
        tmp[n++] = '+';
    }

    while ((n > 0U) && (len < (buf_len - 1U)))
    {
        buf[len++] = tmp[--n];
    }
    buf[len] = '\0';

    return len;
}

/* [] END OF FILE */
