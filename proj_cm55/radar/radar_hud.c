/*******************************************************************************
* File Name        : radar_hud.c
*
* Description      : See radar_hud.h. The 5x7 font covers A-Z, 0-9 and the
*                    symbols the panel needs; '*' renders as a degree sign,
*                    '^' as an up arrow and '~' as a down arrow. Unknown
*                    characters render blank.
*
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "radar_hud.h"

#define FONT_W      (5U)
#define FONT_H      (7U)

#define COLOR_BG        RADAR_HUD_RGB565(0U,   0U,   0U)
#define COLOR_LABEL     RADAR_HUD_RGB565(150U, 150U, 150U)
#define COLOR_DIM       RADAR_HUD_RGB565(70U,  70U,  70U)
#define COLOR_WHITE     RADAR_HUD_RGB565(255U, 255U, 255U)
#define COLOR_CYAN      RADAR_HUD_RGB565(0U,   200U, 255U)
#define COLOR_GREEN     RADAR_HUD_RGB565(0U,   255U, 120U)
#define COLOR_YELLOW    RADAR_HUD_RGB565(255U, 210U, 0U)
#define COLOR_RED       RADAR_HUD_RGB565(255U, 40U,  40U)

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
    { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
    { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
    { '*', { 0x0C, 0x12, 0x12, 0x0C, 0x00, 0x00, 0x00 } },  /* degree sign */
    { '^', { 0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04 } },  /* up arrow    */
    { '~', { 0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04 } },  /* down arrow  */
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
    const uint32_t sc = RADAR_HUD_SCALE;

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
        x += RADAR_HUD_CHAR_ADV;
    }
    return x;
}


static void draw_line(const surface_t* s, uint32_t x, uint32_t y,
                      const char* label, const char* value, uint16_t color)
{
    uint32_t vx = draw_text(s, x, y, label, COLOR_LABEL);
    (void)draw_text(s, vx, y, value, color);
}


size_t radar_format_fixed(char* buf, size_t size, float value,
                          uint32_t decimals, bool plus_sign)
{
    uint32_t scale = 1U;
    uint32_t v;
    bool neg = (value < 0.0f);
    int n;

    if ((NULL == buf) || (0U == size))
    {
        return 0U;
    }
    for (uint32_t i = 0U; i < decimals; i++)
    {
        scale *= 10U;
    }
    if (neg)
    {
        value = -value;
    }
    if (value > 99999.0f)
    {
        value = 99999.0f;
    }
    v = (uint32_t)((value * (float)scale) + 0.5f);
    if (0U == v)
    {
        neg = false;
    }

    if (0U == decimals)
    {
        n = snprintf(buf, size, "%s%lu", neg ? "-" : (plus_sign ? "+" : ""),
                     (unsigned long)v);
    }
    else
    {
        n = snprintf(buf, size, "%s%lu.%0*lu", neg ? "-" : (plus_sign ? "+" : ""),
                     (unsigned long)(v / scale), (int)decimals,
                     (unsigned long)(v % scale));
    }
    if (n < 0)
    {
        buf[0] = '\0';
        return 0U;
    }
    return ((size_t)n < size) ? (size_t)n : (size - 1U);
}


hud_page_t radar_hud_next_page(hud_page_t page, radar_gesture_t g)
{
    switch (g)
    {
        case RADAR_GESTURE_SWIPE_RIGHT:
            return (hud_page_t)(((uint32_t)page + 1U) % (uint32_t)HUD_PAGE_COUNT);
        case RADAR_GESTURE_SWIPE_LEFT:
            return (hud_page_t)(((uint32_t)page + (uint32_t)HUD_PAGE_COUNT - 1U) %
                                (uint32_t)HUD_PAGE_COUNT);
        case RADAR_GESTURE_SWIPE_UP:
            return HUD_PAGE_SPEED;
        case RADAR_GESTURE_SWIPE_DOWN:
            return HUD_PAGE_RADAR;
        case RADAR_GESTURE_PUSH:
        case RADAR_GESTURE_NONE:
        default:
            return page;
    }
}


const char* radar_hud_page_name(hud_page_t page)
{
    switch (page)
    {
        case HUD_PAGE_SPEED: return "SPEED";
        case HUD_PAGE_NAV:   return "NAV";
        case HUD_PAGE_TEAM:  return "TEAM";
        case HUD_PAGE_RADAR: return "RADAR";
        default:             return "?";
    }
}


static const char* gesture_banner(radar_gesture_t g)
{
    switch (g)
    {
        case RADAR_GESTURE_SWIPE_LEFT:  return "<< SWIPE LEFT";
        case RADAR_GESTURE_SWIPE_RIGHT: return "SWIPE RIGHT >>";
        case RADAR_GESTURE_SWIPE_UP:    return "^ SWIPE UP";
        case RADAR_GESTURE_SWIPE_DOWN:  return "~ SWIPE DOWN";
        case RADAR_GESTURE_PUSH:        return "PUSH: SELECT";
        case RADAR_GESTURE_NONE:
        default:                        return "";
    }
}


void radar_hud_draw(uint16_t* fb, uint32_t stride, uint32_t fb_height,
                    uint32_t x, uint32_t y, const radar_hud_view_t* view)
{
    surface_t s = { fb, stride, fb_height };
    char num[12];
    char val[24];
    uint32_t tx = x + RADAR_HUD_PAD;
    uint32_t ty = y + RADAR_HUD_PAD;
    const uint32_t dy = RADAR_HUD_LINE_ADV;
    uint32_t hx;

    if ((NULL == fb) || (NULL == view))
    {
        return;
    }

    fill_rect(&s, x, y, RADAR_HUD_WIDTH, RADAR_HUD_HEIGHT, COLOR_BG);

    /* Line 0: page header + page dots */
    (void)snprintf(val, sizeof(val), "%u/%u %s", (unsigned)view->page + 1U,
                   (unsigned)HUD_PAGE_COUNT, radar_hud_page_name(view->page));
    hx = draw_text(&s, tx, ty, val, COLOR_CYAN);
    hx = x + RADAR_HUD_WIDTH - RADAR_HUD_PAD - ((uint32_t)HUD_PAGE_COUNT * 18U);
    for (uint32_t p = 0U; p < (uint32_t)HUD_PAGE_COUNT; p++)
    {
        fill_rect(&s, hx + (p * 18U), ty + 6U, 10U, 10U,
                  (p == (uint32_t)view->page) ? COLOR_CYAN : COLOR_DIM);
    }

    /* Lines 1..3: page content */
    switch (view->page)
    {
        case HUD_PAGE_SPEED:
            draw_line(&s, tx, ty + dy,        "SPD  ", "-- KM/H", COLOR_WHITE);
            draw_line(&s, tx, ty + (2U * dy), "ALT  ", "---- M", COLOR_WHITE);
            draw_line(&s, tx, ty + (3U * dy), "TIME ", "--:--", COLOR_WHITE);
            break;

        case HUD_PAGE_NAV:
            draw_line(&s, tx, ty + dy,        "HDG  ", "--- *", COLOR_WHITE);
            draw_line(&s, tx, ty + (2U * dy), "WPT  ", "--", COLOR_WHITE);
            draw_line(&s, tx, ty + (3U * dy), "DIST ", "-- M", COLOR_WHITE);
            break;

        case HUD_PAGE_TEAM:
            draw_line(&s, tx, ty + dy,        "LORA ", "0 PEERS", COLOR_WHITE);
            draw_line(&s, tx, ty + (2U * dy), "NEAR ", "-- M", COLOR_WHITE);
            draw_line(&s, tx, ty + (3U * dy), "SOS  ", "OFF", COLOR_GREEN);
            break;

        case HUD_PAGE_RADAR:
        default:
            if (view->present && (RADAR_HUD_MODE_OK == view->mode))
            {
                (void)radar_format_fixed(num, sizeof(num), view->range_cm, 1U, false);
                (void)snprintf(val, sizeof(val), "%s CM", num);
                draw_line(&s, tx, ty + dy, "RNG  ", val, COLOR_GREEN);
                (void)radar_format_fixed(num, sizeof(num), view->azimuth_deg, 0U, true);
                (void)snprintf(val, sizeof(val), "%s *", num);
                draw_line(&s, tx, ty + (2U * dy), "AZ   ", val, COLOR_WHITE);
                (void)radar_format_fixed(num, sizeof(num), view->elevation_deg, 0U, true);
                (void)snprintf(val, sizeof(val), "%s *", num);
                draw_line(&s, tx, ty + (3U * dy), "EL   ", val, COLOR_WHITE);
            }
            else
            {
                draw_line(&s, tx, ty + dy,        "RNG  ", "NO HAND", COLOR_LABEL);
                draw_line(&s, tx, ty + (2U * dy), "AZ   ", "--", COLOR_LABEL);
                draw_line(&s, tx, ty + (3U * dy), "EL   ", "--", COLOR_LABEL);
            }
            break;
    }

    /* Line 4: gesture toast or radar status */
    if (view->toast && (RADAR_GESTURE_NONE != view->last_gesture))
    {
        (void)draw_text(&s, tx, ty + (4U * dy), gesture_banner(view->last_gesture), COLOR_YELLOW);
    }
    else
    {
        switch (view->mode)
        {
            case RADAR_HUD_MODE_OK:
                draw_line(&s, tx, ty + (4U * dy), "RADAR ",
                          view->present ? "HAND" : "READY", COLOR_GREEN);
                break;
            case RADAR_HUD_MODE_NO_RADAR:
                draw_line(&s, tx, ty + (4U * dy), "RADAR ", "N/A BTN2", COLOR_YELLOW);
                break;
            case RADAR_HUD_MODE_ERROR:
                draw_line(&s, tx, ty + (4U * dy), "RADAR ", "ERROR", COLOR_RED);
                break;
            case RADAR_HUD_MODE_WAIT:
            default:
                draw_line(&s, tx, ty + (4U * dy), "RADAR ", "WAIT...", COLOR_YELLOW);
                break;
        }
    }
}

/* [] END OF FILE */
