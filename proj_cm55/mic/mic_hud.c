/*******************************************************************************
* File Name        : mic_hud.c
*
* Description      : 4-channel microphone VU panel (RGB565). See mic_hud.h.
*
*******************************************************************************/
#include "mic_hud.h"

#include <stddef.h>

/*******************************************************************************
* Macros
*******************************************************************************/
#define RGB565(r, g, b)     ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | ((b) >> 3)))

#define COL_BG              RGB565(10U, 12U, 16U)
#define COL_FRAME           RGB565(255U, 0U, 40U)       /* ROG red */
#define COL_TEXT            RGB565(230U, 230U, 230U)
#define COL_DIM             RGB565(110U, 110U, 120U)
#define COL_BAR_BG          RGB565(40U, 44U, 52U)
#define COL_GREEN           RGB565(0U, 220U, 90U)
#define COL_YELLOW          RGB565(255U, 200U, 0U)
#define COL_RED             RGB565(255U, 40U, 40U)
#define COL_CYAN            RGB565(0U, 200U, 255U)
#define COL_HOLD            RGB565(255U, 255U, 255U)

#define FONT_W              (5U)
#define FONT_H              (7U)
#define TEXT_SCALE          (2U)
#define CHAR_ADV            ((FONT_W + 1U) * TEXT_SCALE)

#define PAD                 (8U)
#define TITLE_Y             (6U)
#define ROW0_Y              (32U)
#define ROW_H               (28U)
#define LABEL_X             (PAD)
#define BAR_X               (PAD + (4U * CHAR_ADV))
#define BAR_H               (14U)
#define DB_X                (BAR_X + MIC_HUD_BAR_W + 8U)

/* Bar colour zones (fraction of MIC_HUD_BAR_W) */
#define ZONE_YELLOW         ((MIC_HUD_BAR_W * 70U) / 100U)
#define ZONE_RED            ((MIC_HUD_BAR_W * 90U) / 100U)


/*******************************************************************************
* 5x7 font, column major, bit 0 = top row. Characters ' ' .. 'Z' that the
* panel needs; anything else is drawn as a blank.
*******************************************************************************/
typedef struct
{
    char    c;
    uint8_t col[FONT_W];
} glyph_t;

static const glyph_t font5x7[] =
{
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
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
};

typedef struct
{
    uint16_t *fb;
    uint32_t  stride;
    uint32_t  w;
    uint32_t  h;
} surface_t;


/*******************************************************************************
* Function Name: fill_rect
*******************************************************************************/
static void fill_rect(const surface_t *s, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint16_t color)
{
    if ((x >= s->w) || (y >= s->h))
    {
        return;
    }
    if ((x + w) > s->w)
    {
        w = s->w - x;
    }
    if ((y + h) > s->h)
    {
        h = s->h - y;
    }

    for (uint32_t r = 0U; r < h; r++)
    {
        uint16_t *p = &s->fb[((y + r) * s->stride) + x];
        for (uint32_t c = 0U; c < w; c++)
        {
            p[c] = color;
        }
    }
}


/*******************************************************************************
* Function Name: find_glyph
*******************************************************************************/
static const uint8_t *find_glyph(char c)
{
    if ((c >= 'a') && (c <= 'z'))
    {
        c = (char)(c - ('a' - 'A'));
    }
    for (uint32_t i = 0U; i < (sizeof(font5x7) / sizeof(font5x7[0])); i++)
    {
        if (font5x7[i].c == c)
        {
            return font5x7[i].col;
        }
    }
    return font5x7[0].col;
}


/*******************************************************************************
* Function Name: draw_text
*******************************************************************************/
static void draw_text(const surface_t *s, uint32_t x, uint32_t y,
                      const char *str, uint16_t color)
{
    while ((NULL != str) && ('\0' != *str))
    {
        const uint8_t *g = find_glyph(*str);
        for (uint32_t col = 0U; col < FONT_W; col++)
        {
            for (uint32_t row = 0U; row < FONT_H; row++)
            {
                if (0U != (g[col] & (1U << row)))
                {
                    fill_rect(s, x + (col * TEXT_SCALE), y + (row * TEXT_SCALE),
                              TEXT_SCALE, TEXT_SCALE, color);
                }
            }
        }
        x += CHAR_ADV;
        str++;
    }
}


/*******************************************************************************
* Function Name: format_db
********************************************************************************
* Summary:
*  Writes "-42DB" style text (whole dB, rounded) into buf (>= 7 bytes).
*******************************************************************************/
static void format_db(char *buf, int16_t db10)
{
    int32_t v = db10;
    uint32_t i = 0U;
    uint32_t mag;

    v = (v < 0) ? ((v - 5) / 10) : ((v + 5) / 10);
    if (v < -99)
    {
        v = -99;
    }
    if (v > 9)
    {
        v = 9;
    }

    if (v < 0)
    {
        buf[i++] = '-';
        mag = (uint32_t)(-v);
    }
    else
    {
        buf[i++] = '+';
        mag = (uint32_t)v;
    }
    if (mag >= 10U)
    {
        buf[i++] = (char)('0' + (mag / 10U));
    }
    buf[i++] = (char)('0' + (mag % 10U));
    buf[i++] = 'D';
    buf[i++] = 'B';
    buf[i] = '\0';
}


/*******************************************************************************
* Function Name: draw_bar
*******************************************************************************/
static void draw_bar(const surface_t *s, uint32_t x, uint32_t y,
                     const mic_hud_channel_t *ch)
{
    uint32_t fill = (ch->vu_fill > MIC_HUD_BAR_W) ? MIC_HUD_BAR_W : ch->vu_fill;
    uint32_t hold = (ch->hold_fill > MIC_HUD_BAR_W) ? MIC_HUD_BAR_W : ch->hold_fill;

    fill_rect(s, x, y, MIC_HUD_BAR_W, BAR_H, COL_BAR_BG);

    if (MIC_HUD_CH_OK != ch->status)
    {
        return;
    }

    /* green / yellow / red zones, 1 px gaps every 8 px like an LED meter */
    for (uint32_t px = 0U; px < fill; px++)
    {
        uint16_t col = (px < ZONE_YELLOW) ? COL_GREEN :
                       ((px < ZONE_RED) ? COL_YELLOW : COL_RED);
        if (7U != (px % 8U))
        {
            fill_rect(s, x + px, y, 1U, BAR_H, col);
        }
    }

    if (hold > 1U)
    {
        fill_rect(s, x + hold - 2U, y, 2U, BAR_H, COL_HOLD);
    }
}


/*******************************************************************************
* Function Name: mic_hud_draw
*******************************************************************************/
void mic_hud_draw(uint16_t *fb, uint32_t stride, uint32_t fb_w, uint32_t fb_h,
                  uint32_t x, uint32_t y, const mic_hud_view_t *view)
{
    surface_t s = { fb, stride, fb_w, fb_h };
    char db[8];

    if ((NULL == fb) || (NULL == view))
    {
        return;
    }

    /* Panel background + 2 px ROG red frame */
    fill_rect(&s, x, y, MIC_HUD_WIDTH, MIC_HUD_HEIGHT, COL_BG);
    fill_rect(&s, x, y, MIC_HUD_WIDTH, 2U, COL_FRAME);
    fill_rect(&s, x, y + MIC_HUD_HEIGHT - 2U, MIC_HUD_WIDTH, 2U, COL_FRAME);
    fill_rect(&s, x, y, 2U, MIC_HUD_HEIGHT, COL_FRAME);
    fill_rect(&s, x + MIC_HUD_WIDTH - 2U, y, 2U, MIC_HUD_HEIGHT, COL_FRAME);

    /* Title: mode + analog noise floor */
    draw_text(&s, x + PAD, y + TITLE_Y, "MIC", COL_TEXT);
    if (view->wake_mode)
    {
        draw_text(&s, x + PAD + (4U * CHAR_ADV), y + TITLE_Y,
                  view->vad_active ? "WAKE: ON" : "WAKE: LISTEN",
                  view->vad_active ? COL_CYAN : COL_DIM);
    }
    else
    {
        draw_text(&s, x + PAD + (4U * CHAR_ADV), y + TITLE_Y,
                  view->vad_active ? "VAD: ON" : "VAD: --",
                  view->vad_active ? COL_CYAN : COL_DIM);
    }
    format_db(db, view->floor_db10);
    draw_text(&s, x + MIC_HUD_WIDTH - PAD - (9U * CHAR_ADV), y + TITLE_Y,
              "FLR", COL_DIM);
    draw_text(&s, x + MIC_HUD_WIDTH - PAD - (5U * CHAR_ADV), y + TITLE_Y,
              db, COL_DIM);

    for (uint32_t i = 0U; i < MIC_HUD_CHANNELS; i++)
    {
        const mic_hud_channel_t *ch = &view->ch[i];
        uint32_t ry = y + ROW0_Y + (i * ROW_H);

        draw_text(&s, x + LABEL_X, ry, ch->label, COL_TEXT);
        draw_bar(&s, x + BAR_X, ry, ch);

        switch (ch->status)
        {
            case MIC_HUD_CH_OK:
                format_db(db, ch->rms_db10);
                draw_text(&s, x + DB_X, ry, ch->clipping ? "CLIP" : db,
                          ch->clipping ? COL_RED : COL_TEXT);
                break;
            case MIC_HUD_CH_OFF:
                draw_text(&s, x + DB_X, ry, "OFF", COL_DIM);
                break;
            case MIC_HUD_CH_ERROR:
            default:
                draw_text(&s, x + DB_X, ry, "ERR", COL_RED);
                break;
        }
    }
}

/* [] END OF FILE */
