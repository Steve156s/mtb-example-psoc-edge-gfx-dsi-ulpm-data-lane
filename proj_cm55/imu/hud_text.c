/*******************************************************************************
* File Name        : hud_text.c
*
* Description      : Minimal RGB565 drawing helpers. See hud_text.h.
*
*******************************************************************************/
#include "hud_text.h"

#include <string.h>

/*******************************************************************************
* 5x7 font, column-major, bit 0 = top row.
*******************************************************************************/
typedef struct
{
    char    ch;
    uint8_t col[HUD_FONT_W];
} hud_glyph_t;

static const hud_glyph_t font5x7[] =
{
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '!', { 0x00, 0x00, 0x5F, 0x00, 0x00 } },
    { '%', { 0x23, 0x13, 0x08, 0x64, 0x62 } },
    { '(', { 0x00, 0x1C, 0x22, 0x41, 0x00 } },
    { ')', { 0x00, 0x41, 0x22, 0x1C, 0x00 } },
    { '+', { 0x08, 0x08, 0x3E, 0x08, 0x08 } },
    { '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } },
    { '.', { 0x00, 0x60, 0x60, 0x00, 0x00 } },
    { '/', { 0x20, 0x10, 0x08, 0x04, 0x02 } },
    { '0', { 0x3E, 0x51, 0x49, 0x45, 0x3E } },
    { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
    { '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } },
    { '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } },
    { '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } },
    { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
    { '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } },
    { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
    { '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
    { '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
    { ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } },
    { '=', { 0x14, 0x14, 0x14, 0x14, 0x14 } },
    { 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } },
    { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } },
    { 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } },
    { 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } },
    { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
    { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } },
    { 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } },
    { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
    { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
    { 'J', { 0x20, 0x40, 0x41, 0x3F, 0x01 } },
    { 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } },
    { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
    { 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } },
    { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
    { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
    { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
    { 'Q', { 0x3E, 0x41, 0x51, 0x21, 0x5E } },
    { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
    { 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
    { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
    { 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } },
    { 'V', { 0x1F, 0x20, 0x40, 0x20, 0x1F } },
    { 'W', { 0x3F, 0x40, 0x38, 0x40, 0x3F } },
    { 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } },
    { 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } },
    { 'Z', { 0x61, 0x51, 0x49, 0x45, 0x43 } },
    { '|', { 0x00, 0x00, 0x7F, 0x00, 0x00 } },
};

/* Unknown characters draw as a hollow box */
static const uint8_t glyph_unknown[HUD_FONT_W] = { 0x7F, 0x41, 0x41, 0x41, 0x7F };

static const uint8_t *find_glyph(char c)
{
    size_t i;

    if ((c >= 'a') && (c <= 'z'))
    {
        c = (char)(c - 'a' + 'A');
    }
    for (i = 0U; i < (sizeof(font5x7) / sizeof(font5x7[0])); i++)
    {
        if (font5x7[i].ch == c)
        {
            return font5x7[i].col;
        }
    }
    return glyph_unknown;
}

void hud_fill_rect(const hud_surface_t *s, int32_t x, int32_t y,
                   int32_t w, int32_t h, uint16_t color)
{
    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x + w;
    int32_t y1 = y + h;
    int32_t row;
    int32_t col;

    if ((NULL == s) || (NULL == s->pixels))
    {
        return;
    }
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 > (int32_t)s->width)  { x1 = (int32_t)s->width; }
    if (y1 > (int32_t)s->height) { y1 = (int32_t)s->height; }

    for (row = y0; row < y1; row++)
    {
        uint16_t *line = &s->pixels[(uint32_t)row * s->stride];
        for (col = x0; col < x1; col++)
        {
            line[col] = color;
        }
    }
}

int32_t hud_draw_text(const hud_surface_t *s, int32_t x, int32_t y,
                      const char *text, uint32_t scale,
                      uint16_t fg, uint16_t bg)
{
    int32_t sc = (int32_t)((0U == scale) ? 1U : scale);
    int32_t advance = (int32_t)(HUD_FONT_W + 1U) * sc;

    if ((NULL == s) || (NULL == text))
    {
        return x;
    }

    for (; '\0' != *text; text++)
    {
        const uint8_t *g = find_glyph(*text);
        uint32_t cx;
        uint32_t cy;

        /* Cell background including the 1-pixel spacing column */
        hud_fill_rect(s, x, y, advance, (int32_t)HUD_FONT_H * sc, bg);

        for (cx = 0U; cx < HUD_FONT_W; cx++)
        {
            for (cy = 0U; cy < HUD_FONT_H; cy++)
            {
                if (0U != (g[cx] & (1U << cy)))
                {
                    hud_fill_rect(s, x + ((int32_t)cx * sc),
                                  y + ((int32_t)cy * sc), sc, sc, fg);
                }
            }
        }
        x += advance;
    }
    return x;
}

void hud_format_fixed1(char *buf, size_t buf_len, float value, uint32_t width)
{
    char tmp[16];
    size_t n = 0U;
    size_t len;
    size_t pad;
    int32_t tenths;
    uint32_t mag;
    char digits[12];
    size_t nd = 0U;

    if ((NULL == buf) || (0U == buf_len))
    {
        return;
    }

    /* Round to nearest tenth, clamp to a sane range */
    if (value > 99999.0f)  { value = 99999.0f; }
    if (value < -99999.0f) { value = -99999.0f; }
    tenths = (int32_t)((value >= 0.0f) ? (value * 10.0f + 0.5f)
                                       : (value * 10.0f - 0.5f));

    if (tenths < 0)
    {
        tmp[n++] = '-';
        mag = (uint32_t)(-tenths);
    }
    else
    {
        mag = (uint32_t)tenths;
    }

    /* Integer part */
    {
        uint32_t ip = mag / 10U;
        do
        {
            digits[nd++] = (char)('0' + (ip % 10U));
            ip /= 10U;
        } while ((0U != ip) && (nd < sizeof(digits)));
        while (nd > 0U)
        {
            tmp[n++] = digits[--nd];
        }
    }
    tmp[n++] = '.';
    tmp[n++] = (char)('0' + (mag % 10U));
    tmp[n]   = '\0';

    len = n;
    pad = (width > len) ? (width - len) : 0U;
    if ((pad + len + 1U) > buf_len)
    {
        pad = 0U;
        if ((len + 1U) > buf_len)
        {
            len = buf_len - 1U;
        }
    }
    memset(buf, ' ', pad);
    memcpy(&buf[pad], tmp, len);
    buf[pad + len] = '\0';
}
