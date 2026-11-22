/*******************************************************************************
* File Name        : rtc_hud.c
*
* Description      : HUD clock panel renderer. See rtc_hud.h.
*                    Hours and minutes use scalable 7-segment digits (no large
*                    font table in flash); the rest uses a 5x7 font scaled 2x
*                    or 3x. Unknown characters render blank.
*
*******************************************************************************/
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "rtc_hud.h"

#define FONT_W          (5U)
#define FONT_H          (7U)

#define COLOR_BG        RTC_HUD_RGB565(0U,   0U,   0U)
#define COLOR_FRAME     RTC_HUD_RGB565(0U,   90U,  50U)
#define COLOR_LABEL     RTC_HUD_RGB565(150U, 150U, 150U)
#define COLOR_GREEN     RTC_HUD_RGB565(0U,   255U, 120U)
#define COLOR_CYAN      RTC_HUD_RGB565(0U,   200U, 255U)
#define COLOR_WHITE     RTC_HUD_RGB565(255U, 255U, 255U)
#define COLOR_AMBER     RTC_HUD_RGB565(255U, 170U, 0U)
#define COLOR_RED       RTC_HUD_RGB565(255U, 40U,  40U)
#define COLOR_SEG_OFF   RTC_HUD_RGB565(0U,   28U,  14U)

/* 7-segment geometry */
#define SEG_W           (40U)   /* digit width                */
#define SEG_H           (64U)   /* digit height               */
#define SEG_T           (8U)    /* segment thickness          */
#define SEG_GAP         (8U)    /* gap between two digits     */
#define COLON_W         (20U)

/* Panel layout (offsets from the panel origin) */
#define ROW_TITLE_Y     (8U)
#define DIGITS_X        (10U)
#define DIGITS_Y        (30U)
#define SECONDS_X       (226U)
#define ROW_DATE_Y      (104U)
#define ROW_STATUS_Y    (128U)

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
    { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
};

typedef struct
{
    uint16_t *fb;
    uint32_t  stride;
    uint32_t  height;
} surface_t;


static const uint8_t *find_glyph(char ch)
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


static void fill_rect(const surface_t *s, uint32_t x, uint32_t y,
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
        uint16_t *p = &s->fb[(yy * s->stride) + x];
        for (uint32_t xx = 0U; xx < w; xx++)
        {
            p[xx] = color;
        }
    }
}


/* Returns the x position after the text. */
static uint32_t draw_text(const surface_t *s, uint32_t x, uint32_t y,
                          const char *text, uint32_t sc, uint16_t fg)
{
    for (; '\0' != *text; text++)
    {
        const uint8_t *g = find_glyph(*text);

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
        x += (FONT_W + 1U) * sc;
    }
    return x;
}


static uint32_t text_width(const char *text, uint32_t sc)
{
    return (uint32_t)strlen(text) * (FONT_W + 1U) * sc;
}


/* Segment bit order: a b c d e f g (bit0 = a) */
static uint8_t seg_mask(char ch)
{
    static const uint8_t digits[10] =
    {
        0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U, 0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU
    };

    if ((ch >= '0') && (ch <= '9'))
    {
        return digits[ch - '0'];
    }
    return (ch == '-') ? 0x40U : 0x00U;
}


/* Draws one 7-segment digit. Unlit segments are drawn in a dim colour, which
 * on a see-through HUD reads as a faint "ghost" and keeps the layout stable. */
static void draw_seg_digit(const surface_t *s, uint32_t x, uint32_t y,
                           char ch, uint16_t on)
{
    const uint8_t m = seg_mask(ch);
    const uint32_t hh = (SEG_H - (3U * SEG_T)) / 2U;   /* vertical segment length */
    const uint32_t hw = SEG_W - (2U * SEG_T);          /* horizontal segment length */
    const uint32_t ymid = y + SEG_T + hh;

    /* a, d, g: horizontal */
    fill_rect(s, x + SEG_T, y,                      hw, SEG_T, (m & 0x01U) ? on : COLOR_SEG_OFF);
    fill_rect(s, x + SEG_T, y + SEG_H - SEG_T,      hw, SEG_T, (m & 0x08U) ? on : COLOR_SEG_OFF);
    fill_rect(s, x + SEG_T, ymid,                   hw, SEG_T, (m & 0x40U) ? on : COLOR_SEG_OFF);
    /* f, b: upper vertical */
    fill_rect(s, x,                  y + SEG_T, SEG_T, hh, (m & 0x20U) ? on : COLOR_SEG_OFF);
    fill_rect(s, x + SEG_W - SEG_T,  y + SEG_T, SEG_T, hh, (m & 0x02U) ? on : COLOR_SEG_OFF);
    /* e, c: lower vertical */
    fill_rect(s, x,                  ymid + SEG_T, SEG_T, hh, (m & 0x10U) ? on : COLOR_SEG_OFF);
    fill_rect(s, x + SEG_W - SEG_T,  ymid + SEG_T, SEG_T, hh, (m & 0x04U) ? on : COLOR_SEG_OFF);
}


char *rtc_hud_format_age(uint32_t age_s, char *buf, uint32_t len)
{
    if (age_s < 60U)
    {
        (void)snprintf(buf, len, "%luS", (unsigned long)age_s);
    }
    else if (age_s < 3600U)
    {
        (void)snprintf(buf, len, "%luM", (unsigned long)(age_s / 60U));
    }
    else if (age_s < 86400U)
    {
        (void)snprintf(buf, len, "%luH", (unsigned long)(age_s / 3600U));
    }
    else
    {
        (void)snprintf(buf, len, "%luD", (unsigned long)(age_s / 86400U));
    }
    return buf;
}


void rtc_hud_draw(uint16_t *fb, uint32_t stride, uint32_t height,
                  uint32_t x, uint32_t y, const rtc_hud_view_t *view)
{
    const surface_t s = { fb, stride, height };
    char hhmm[6];
    char line[40];
    char tmp[16];
    uint16_t digit_color;
    uint32_t dx;

    if ((NULL == fb) || (NULL == view))
    {
        return;
    }

    /* background and a 1 px frame */
    fill_rect(&s, x, y, RTC_HUD_WIDTH, RTC_HUD_HEIGHT, COLOR_BG);
    fill_rect(&s, x, y, RTC_HUD_WIDTH, 1U, COLOR_FRAME);
    fill_rect(&s, x, y + RTC_HUD_HEIGHT - 1U, RTC_HUD_WIDTH, 1U, COLOR_FRAME);
    fill_rect(&s, x, y, 1U, RTC_HUD_HEIGHT, COLOR_FRAME);
    fill_rect(&s, x + RTC_HUD_WIDTH - 1U, y, 1U, RTC_HUD_HEIGHT, COLOR_FRAME);

    /* title row: "CLOCK" ......... "UTC+08:00" */
    (void)draw_text(&s, x + 10U, y + ROW_TITLE_Y, "CLOCK", 2U, COLOR_LABEL);
    (void)rtc_time_format_tz(view->tz_min, tmp, sizeof(tmp));
    (void)draw_text(&s, x + RTC_HUD_WIDTH - 10U - text_width(tmp, 2U),
                    y + ROW_TITLE_Y, tmp, 2U, COLOR_CYAN);

    /* big HH:MM */
    if (view->time_valid)
    {
        hhmm[0] = (char)('0' + ((view->local.hour / 10U) % 10U));
        hhmm[1] = (char)('0' + (view->local.hour % 10U));
        hhmm[2] = (char)('0' + ((view->local.min / 10U) % 10U));
        hhmm[3] = (char)('0' + (view->local.min % 10U));
        hhmm[4] = '\0';
        digit_color = COLOR_GREEN;
    }
    else
    {
        (void)strcpy(hhmm, "----");
        digit_color = COLOR_AMBER;
    }

    dx = x + DIGITS_X;
    draw_seg_digit(&s, dx, y + DIGITS_Y, hhmm[0], digit_color);
    dx += SEG_W + SEG_GAP;
    draw_seg_digit(&s, dx, y + DIGITS_Y, hhmm[1], digit_color);
    dx += SEG_W;
    /* colon: steady (blinking would cost a redraw every 0.5 s) */
    fill_rect(&s, dx + ((COLON_W - SEG_T) / 2U), y + DIGITS_Y + 16U, SEG_T, SEG_T, digit_color);
    fill_rect(&s, dx + ((COLON_W - SEG_T) / 2U), y + DIGITS_Y + SEG_H - 24U, SEG_T, SEG_T, digit_color);
    dx += COLON_W;
    draw_seg_digit(&s, dx, y + DIGITS_Y, hhmm[2], digit_color);
    dx += SEG_W + SEG_GAP;
    draw_seg_digit(&s, dx, y + DIGITS_Y, hhmm[3], digit_color);

    /* seconds (3x font, bottom aligned with the big digits). In low-power
     * mode the panel is redrawn once a minute, so seconds are not shown. */
    if (view->low_power)
    {
        (void)draw_text(&s, x + SECONDS_X, y + DIGITS_Y, "LP", 2U, COLOR_AMBER);
        (void)draw_text(&s, x + SECONDS_X, y + DIGITS_Y + SEG_H - 21U, "--", 3U, COLOR_LABEL);
    }
    else
    {
        tmp[0] = (char)('0' + ((view->local.sec / 10U) % 10U));
        tmp[1] = (char)('0' + (view->local.sec % 10U));
        tmp[2] = '\0';
        (void)draw_text(&s, x + SECONDS_X, y + DIGITS_Y + SEG_H - 21U,
                        view->time_valid ? tmp : "--", 3U, COLOR_WHITE);
    }

    /* date row: "2026-11-22 SUN" */
    if (view->time_valid)
    {
        (void)rtc_time_format_date(&view->local, tmp, sizeof(tmp));
        (void)snprintf(line, sizeof(line), "%s %s", tmp, rtc_time_wday_name(view->local.wday));
        (void)draw_text(&s, x + 10U, y + ROW_DATE_Y, line, 2U, COLOR_WHITE);
    }
    else
    {
        (void)draw_text(&s, x + 10U, y + ROW_DATE_Y, "----------", 2U, COLOR_LABEL);
    }

    /* status row */
    if (!view->wco_ok)
    {
        (void)draw_text(&s, x + 10U, y + ROW_STATUS_Y, "WCO FAIL - CHECK XTAL", 2U, COLOR_RED);
    }
    else if (!view->time_valid)
    {
        (void)draw_text(&s, x + 10U, y + ROW_STATUS_Y, "SET TIME: UART T CMD", 2U, COLOR_AMBER);
    }
    else
    {
        (void)rtc_hud_format_age(view->sync_age_s, tmp, sizeof(tmp));
        (void)snprintf(line, sizeof(line), "SYNC %s %s",
                       (NULL != view->src_name) ? view->src_name : "NONE", tmp);
        (void)draw_text(&s, x + 10U, y + ROW_STATUS_Y, line, 2U, COLOR_LABEL);
    }
}
