/*******************************************************************************
* File Name        : compass_hud.c
*
* Description      : HUD compass tape renderer (RGB565). See compass_hud.h.
*
*                    Layout of the top band (y in pixels):
*                       8..21   tape labels  (N, 30, 60, NE, ...)
*                      28..50   tape ticks   (5 deg minor, 10 deg major)
*                      54..64   centre marker
*                      72..99   big readout  "273° W"   + status on the right
*
*******************************************************************************/
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "compass_hud.h"
#include "compass_math.h"

/*******************************************************************************
* Colours (RGB565)
*******************************************************************************/
#define HUD_COLOR_BG            (0x0000U)   /* black            */
#define HUD_COLOR_FG            (0x07E0U)   /* HUD green        */
#define HUD_COLOR_DIM           (0x0320U)   /* dim green        */
#define HUD_COLOR_WARN          (0xFD20U)   /* amber            */
#define HUD_COLOR_ERR           (0xF800U)   /* red              */

/*******************************************************************************
* Layout
*******************************************************************************/
#define HUD_PX_PER_DEG          (5)
#define HUD_TAPE_MARGIN_PX      (40)
#define HUD_LABEL_Y             (8)
#define HUD_TICK_BASE_Y         (50)
#define HUD_TICK_MAJOR_H        (20)
#define HUD_TICK_MINOR_H        (10)
#define HUD_MARKER_Y            (54)
#define HUD_READOUT_Y           (72)
#define HUD_LABEL_SCALE         (2)
#define HUD_READOUT_SCALE       (4)
#define HUD_STATUS_SCALE        (2)

#define GLYPH_W                 (5)
#define GLYPH_H                 (7)

/*******************************************************************************
* 5x7 font, only the characters the HUD needs. '^' renders as a degree sign.
*******************************************************************************/
typedef struct
{
    char c;
    uint8_t rows[GLYPH_H];
} glyph_t;

static const glyph_t font5x7[] =
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
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '?', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } },
    { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
    { '^', { 0x0C, 0x12, 0x12, 0x0C, 0x00, 0x00, 0x00 } },
};

/*******************************************************************************
* Drawing context
*******************************************************************************/
typedef struct
{
    uint16_t *fb;
    int32_t stride;
    int32_t width;
    int32_t height;
} canvas_t;

static void fill_rect(const canvas_t *cv, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t x1 = ((x + w) > cv->width) ? cv->width : (x + w);
    int32_t y1 = ((y + h) > cv->height) ? cv->height : (y + h);

    for (int32_t yy = y0; yy < y1; yy++)
    {
        uint16_t *row = &cv->fb[(yy * cv->stride) + x0];
        for (int32_t xx = x0; xx < x1; xx++)
        {
            *row++ = color;
        }
    }
}

static const uint8_t *find_glyph(char c)
{
    for (uint32_t i = 0U; i < (sizeof(font5x7) / sizeof(font5x7[0])); i++)
    {
        if (font5x7[i].c == c)
        {
            return font5x7[i].rows;
        }
    }
    return NULL;   /* space / unknown: blank */
}

static int32_t text_width(const char *s, int32_t scale)
{
    int32_t n = (int32_t) strlen(s);
    return (n > 0) ? ((n * (GLYPH_W + 1) - 1) * scale) : 0;
}

static void draw_text(const canvas_t *cv, int32_t x, int32_t y, const char *s, int32_t scale, uint16_t color)
{
    for (; '\0' != *s; s++)
    {
        const uint8_t *g = find_glyph(*s);

        if (NULL != g)
        {
            for (int32_t r = 0; r < GLYPH_H; r++)
            {
                for (int32_t c = 0; c < GLYPH_W; c++)
                {
                    if (0U != (g[r] & (0x10U >> c)))
                    {
                        fill_rect(cv, x + (c * scale), y + (r * scale), scale, scale, color);
                    }
                }
            }
        }
        x += (GLYPH_W + 1) * scale;
    }
}

static void draw_text_centered(const canvas_t *cv, int32_t cx, int32_t y, const char *s, int32_t scale, uint16_t color)
{
    draw_text(cv, cx - (text_width(s, scale) / 2), y, s, scale, color);
}

/* Small upward pointing triangle (tip at y) */
static void draw_marker(const canvas_t *cv, int32_t cx, int32_t y, uint16_t color)
{
    for (int32_t i = 0; i < 10; i++)
    {
        fill_rect(cv, cx - i, y + i, (2 * i) + 1, 1, color);
    }
}

/*******************************************************************************
* Tape
*******************************************************************************/
static void draw_tape(const canvas_t *cv, float heading)
{
    static const char *const cardinal[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    const int32_t cx = cv->width / 2;
    const int32_t half_span_deg = ((cv->width / 2) - HUD_TAPE_MARGIN_PX) / HUD_PX_PER_DEG;
    const int32_t first = (int32_t) floorf(heading) - half_span_deg;
    const int32_t last  = (int32_t) ceilf(heading) + half_span_deg;
    char buf[8];

    for (int32_t d = first; d <= last; d++)
    {
        int32_t dw = ((d % 360) + 360) % 360;
        int32_t x;

        if (0 != (dw % 5))
        {
            continue;
        }

        x = cx + (int32_t) lroundf(((float) d - heading) * (float) HUD_PX_PER_DEG);
        if ((x < HUD_TAPE_MARGIN_PX) || (x >= (cv->width - HUD_TAPE_MARGIN_PX)))
        {
            continue;
        }

        if (0 == (dw % 10))
        {
            fill_rect(cv, x - 1, HUD_TICK_BASE_Y - HUD_TICK_MAJOR_H, 3, HUD_TICK_MAJOR_H, HUD_COLOR_FG);
        }
        else
        {
            fill_rect(cv, x, HUD_TICK_BASE_Y - HUD_TICK_MINOR_H, 1, HUD_TICK_MINOR_H, HUD_COLOR_DIM);
        }

        if (0 == (dw % 45))
        {
            draw_text_centered(cv, x, HUD_LABEL_Y, cardinal[dw / 45], HUD_LABEL_SCALE,
                               (0 == dw) ? HUD_COLOR_WARN : HUD_COLOR_FG);
        }
        else if (0 == (dw % 30))
        {
            (void) snprintf(buf, sizeof(buf), "%d", (int) dw);
            draw_text_centered(cv, x, HUD_LABEL_Y, buf, HUD_LABEL_SCALE, HUD_COLOR_DIM);
        }
    }

    /* baseline */
    fill_rect(cv, HUD_TAPE_MARGIN_PX, HUD_TICK_BASE_Y, cv->width - (2 * HUD_TAPE_MARGIN_PX), 2, HUD_COLOR_DIM);
}

/*******************************************************************************
* Public
*******************************************************************************/
void compass_hud_draw(uint16_t *fb, uint32_t stride_px, uint32_t width_px,
                      const compass_hud_view_t *view)
{
    canvas_t cv;
    char buf[24];
    const int32_t status_x = (int32_t) width_px - HUD_TAPE_MARGIN_PX;

    cv.fb = fb;
    cv.stride = (int32_t) stride_px;
    cv.width = (int32_t) ((width_px < stride_px) ? width_px : stride_px);
    cv.height = (int32_t) COMPASS_HUD_BAND_HEIGHT;

    fill_rect(&cv, 0, 0, cv.width, cv.height, HUD_COLOR_BG);

    switch (view->mode)
    {
        case COMPASS_HUD_MODE_HEADING:
        case COMPASS_HUD_MODE_CALIBRATING:
        {
            float h = compass_wrap_360(view->heading_deg);
            int32_t hi = (int32_t) lroundf(h) % 360;

            draw_tape(&cv, h);
            draw_marker(&cv, cv.width / 2, HUD_MARKER_Y, HUD_COLOR_WARN);

            (void) snprintf(buf, sizeof(buf), "%03d^ %s", (int) hi, compass_cardinal_8(h));
            draw_text_centered(&cv, cv.width / 2, HUD_READOUT_Y, buf, HUD_READOUT_SCALE,
                               view->interference ? HUD_COLOR_WARN : HUD_COLOR_FG);

            if (COMPASS_HUD_MODE_CALIBRATING == view->mode)
            {
                int32_t bar_w = 120;
                int32_t fill_w = (bar_w * (int32_t) view->cal_progress) / 100;

                (void) snprintf(buf, sizeof(buf), "CAL %u%%", (unsigned int) view->cal_progress);
                draw_text(&cv, status_x - text_width(buf, HUD_STATUS_SCALE), HUD_READOUT_Y, buf,
                          HUD_STATUS_SCALE, HUD_COLOR_WARN);
                fill_rect(&cv, status_x - bar_w, HUD_READOUT_Y + 20, bar_w, 6, HUD_COLOR_DIM);
                fill_rect(&cv, status_x - bar_w, HUD_READOUT_Y + 20, fill_w, 6, HUD_COLOR_WARN);
            }
            else
            {
                int32_t y = HUD_READOUT_Y;

                if (view->interference)
                {
                    draw_text(&cv, status_x - text_width("MAG?", HUD_STATUS_SCALE), y, "MAG?",
                              HUD_STATUS_SCALE, HUD_COLOR_WARN);
                    y += 16;
                }
                if (!view->cal_valid)
                {
                    draw_text(&cv, status_x - text_width("CAL?", HUD_STATUS_SCALE), y, "CAL?",
                              HUD_STATUS_SCALE, HUD_COLOR_DIM);
                    y += 16;
                }
                if (!view->tilt_compensated)
                {
                    draw_text(&cv, HUD_TAPE_MARGIN_PX, HUD_READOUT_Y, "LEVEL", HUD_STATUS_SCALE,
                              HUD_COLOR_DIM);
                }
            }
            break;
        }

        case COMPASS_HUD_MODE_NO_SENSOR:
            draw_text_centered(&cv, cv.width / 2, 40, "NO MAG", HUD_READOUT_SCALE, HUD_COLOR_ERR);
            break;

        case COMPASS_HUD_MODE_ERROR:
            draw_text_centered(&cv, cv.width / 2, 40, "MAG ERR", HUD_READOUT_SCALE, HUD_COLOR_ERR);
            break;

        case COMPASS_HUD_MODE_WAIT:
        default:
            draw_text_centered(&cv, cv.width / 2, 40, "---^", HUD_READOUT_SCALE, HUD_COLOR_DIM);
            break;
    }
}
