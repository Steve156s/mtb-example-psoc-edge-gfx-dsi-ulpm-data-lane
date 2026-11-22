/*******************************************************************************
* File Name        : sht40_hud.c
*
* Description      : See sht40_hud.h. The 5x7 font covers A-Z, 0-9 and the
*                    few symbols the panel needs; '*' renders as a degree
*                    sign. Unknown characters render blank.
*
*******************************************************************************/
#include <stddef.h>
#include <stdio.h>

#include "sht40_hud.h"

#define FONT_W      (5U)
#define FONT_H      (7U)

#define COLOR_BG        SHT40_HUD_RGB565(0U,   0U,   0U)
#define COLOR_LABEL     SHT40_HUD_RGB565(150U, 150U, 150U)
#define COLOR_WHITE     SHT40_HUD_RGB565(255U, 255U, 255U)
#define COLOR_CYAN      SHT40_HUD_RGB565(0U,   200U, 255U)
#define COLOR_GREEN     SHT40_HUD_RGB565(0U,   255U, 120U)
#define COLOR_YELLOW    SHT40_HUD_RGB565(255U, 210U, 0U)
#define COLOR_RED       SHT40_HUD_RGB565(255U, 40U,  40U)
#define COLOR_ORANGE    SHT40_HUD_RGB565(255U, 120U, 0U)

typedef struct
{
    char    ch;
    uint8_t rows[FONT_H];       /* bit4 = leftmost pixel */
} glyph_t;

static const glyph_t font[] =
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
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
    { '/', { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 } },
    { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
    { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
    { '*', { 0x0C, 0x12, 0x12, 0x0C, 0x00, 0x00, 0x00 } },  /* degree sign */
};

typedef struct
{
    uint16_t* fb;
    uint32_t  stride;
    uint32_t  height;
} surface_t;


static const uint8_t* find_glyph(char ch)
{
    for (size_t i = 0U; i < (sizeof(font) / sizeof(font[0])); i++)
    {
        if (font[i].ch == ch)
        {
            return font[i].rows;
        }
    }
    return NULL;
}


static void fill_rect(const surface_t* s, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint16_t color)
{
    if ((x >= s->stride) || (y >= s->height))
    {
        return;
    }
    if ((x + w) > s->stride)
    {
        w = s->stride - x;
    }
    if ((y + h) > s->height)
    {
        h = s->height - y;
    }
    for (uint32_t yy = y; yy < (y + h); yy++)
    {
        uint16_t* p = &s->fb[(yy * s->stride) + x];
        for (uint32_t xx = 0U; xx < w; xx++)
        {
            p[xx] = color;
        }
    }
}


static uint32_t draw_text(const surface_t* s, uint32_t x, uint32_t y,
                          const char* text, uint16_t fg)
{
    const uint32_t sc = SHT40_HUD_SCALE;

    for (; '\0' != *text; text++)
    {
        const uint8_t* g = find_glyph(*text);

        if (NULL != g)
        {
            for (uint32_t r = 0U; r < FONT_H; r++)
            {
                for (uint32_t c = 0U; c < FONT_W; c++)
                {
                    if (0U != (g[r] & (0x10U >> c)))
                    {
                        fill_rect(s, x + (c * sc), y + (r * sc), sc, sc, fg);
                    }
                }
            }
        }
        x += SHT40_HUD_CHAR_ADV;
    }
    return x;
}


/* "LABEL" in grey followed by a value string in 'color' */
static void draw_line(const surface_t* s, uint32_t x, uint32_t y,
                      const char* label, const char* value, uint16_t color)
{
    uint32_t vx = draw_text(s, x, y, label, COLOR_LABEL);
    (void)draw_text(s, vx, y, value, color);
}


void sht40_hud_draw(uint16_t* fb, uint32_t stride, uint32_t fb_height,
                    uint32_t x, uint32_t y, const sht40_hud_view_t* view)
{
    surface_t s = { fb, stride, fb_height };
    char num[8];        /* "-999.9" fits; longer values are truncated */
    char val[24];
    uint32_t tx = x + SHT40_HUD_PAD;
    uint32_t ty = y + SHT40_HUD_PAD;
    const uint32_t dy = SHT40_HUD_LINE_ADV;

    if ((NULL == fb) || (NULL == view))
    {
        return;
    }

    fill_rect(&s, x, y, SHT40_HUD_WIDTH, SHT40_HUD_HEIGHT, COLOR_BG);

    switch (view->mode)
    {
        case SHT40_HUD_MODE_NO_SENSOR:
            draw_text(&s, tx, ty, "SHT40", COLOR_WHITE);
            draw_text(&s, tx, ty + dy, "NO SENSOR", COLOR_RED);
            return;

        case SHT40_HUD_MODE_ERROR:
            draw_text(&s, tx, ty, "SHT40", COLOR_WHITE);
            draw_text(&s, tx, ty + dy, "READ ERROR", COLOR_RED);
            return;

        case SHT40_HUD_MODE_WAIT:
            draw_text(&s, tx, ty, "SHT40", COLOR_WHITE);
            draw_text(&s, tx, ty + dy, "WAIT...", COLOR_YELLOW);
            return;

        case SHT40_HUD_MODE_DATA:
        case SHT40_HUD_MODE_HEATING:
        default:
            break;
    }

    (void)sht40_format_fixed(num, sizeof(num), view->temperature_c, 1U, false);
    (void)snprintf(val, sizeof(val), "%7s *C", num);
    draw_line(&s, tx, ty, "T    ", val, COLOR_WHITE);

    (void)sht40_format_fixed(num, sizeof(num), view->humidity_pct, 1U, false);
    (void)snprintf(val, sizeof(val), "%7s %%", num);
    draw_line(&s, tx, ty + dy, "RH   ", val, COLOR_CYAN);

    (void)sht40_format_fixed(num, sizeof(num), view->dew_point_c, 1U, false);
    (void)snprintf(val, sizeof(val), "%7s *C", num);
    draw_line(&s, tx, ty + (2U * dy), "DEW  ", val, COLOR_WHITE);

    if (SHT40_HUD_MODE_HEATING == view->mode)
    {
        draw_line(&s, tx, ty + (3U * dy), "SNS  ", "DRYING", COLOR_ORANGE);
    }
    else
    {
        switch (view->fog_level)
        {
            case SHT40_FOG_LIKELY:
                draw_line(&s, tx, ty + (3U * dy), "LENS ", "FOG!", COLOR_RED);
                break;
            case SHT40_FOG_RISK:
                draw_line(&s, tx, ty + (3U * dy), "LENS ", "FOG RISK", COLOR_YELLOW);
                break;
            case SHT40_FOG_OK:
            default:
                draw_line(&s, tx, ty + (3U * dy), "LENS ", "CLEAR", COLOR_GREEN);
                break;
        }
    }

    if (view->batt_heater_on)
    {
        draw_line(&s, tx, ty + (4U * dy), "BATT ", "HEAT ON", COLOR_ORANGE);
    }
    else
    {
        draw_line(&s, tx, ty + (4U * dy), "BATT ", "HEAT OFF", COLOR_GREEN);
    }
}

/* [] END OF FILE */
