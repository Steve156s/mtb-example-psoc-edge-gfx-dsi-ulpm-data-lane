/*******************************************************************************
* File Name        : goggle_link_hud.c
*
* Description      : See goggle_link_hud.h.
*
*                    Panel layout (20 columns x 8 lines):
*                      0  LINK TRK PH WIFI          (green = up, grey = down)
*                         or PAIR CODE 123456        (while pairing)
*                      1  GPS 3D 09SAT / GPS NO FIX / GPS LOST 12S
*                      2  25.0331N 121.5654E
*                      3  ALT 2345M 34KMH NE
*                      4  TEAM 3   SOS ANNA
*                      5  ANNA NE 2.6KM +85M!      (SOS first, then nearest)
*                      6  KEN  N  320M  +12M<      (< = talking on PTT)
*                      7  MSG MEET AT LIFT 3
*
*                    The bearing is relative to true north (no goggle
*                    compass on this branch); with the BMM350 heading it
*                    becomes gl_relative_bearing(bearing, heading).
*
*******************************************************************************/
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "goggle_link_hud.h"

#define FONT_W          (5U)
#define FONT_H          (7U)

#define COLOR_BG        GL_HUD_RGB565(0U,   0U,   0U)
#define COLOR_FRAME     GL_HUD_RGB565(0U,   90U,  140U)
#define COLOR_LABEL     GL_HUD_RGB565(150U, 150U, 150U)
#define COLOR_DIM       GL_HUD_RGB565(80U,  80U,  80U)
#define COLOR_WHITE     GL_HUD_RGB565(255U, 255U, 255U)
#define COLOR_CYAN      GL_HUD_RGB565(0U,   200U, 255U)
#define COLOR_GREEN     GL_HUD_RGB565(0U,   255U, 120U)
#define COLOR_YELLOW    GL_HUD_RGB565(255U, 210U, 0U)
#define COLOR_RED       GL_HUD_RGB565(255U, 40U,  40U)

#define MAX_SEGS        (6U)
#define SEG_LEN         (GL_HUD_COLS + 1U)
#define TMP_LEN         (48U)    /* format buffer; text is cut to SEG_LEN */

typedef struct
{
    char     text[SEG_LEN];
    uint16_t color;
} seg_t;

typedef struct
{
    uint32_t n;
    seg_t    s[MAX_SEGS];
} line_t;

/*******************************************************************************
* 5x7 font: space, A-Z, 0-9 and punctuation. Unknown characters render blank.
*******************************************************************************/
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
    { ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
    { '/', { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 } },
    { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
    { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
    { '?', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } },
    { '>', { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 } },
    { '<', { 0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01 } },
    { '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
    { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
    { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
    { '#', { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A } },
    { '\'', { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
    { '"', { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '@', { 0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E } },
    { '&', { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D } },
    { '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
    { '*', { 0x0C, 0x12, 0x12, 0x0C, 0x00, 0x00, 0x00 } },  /* degree sign */
};

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

/*******************************************************************************
* Line builders
*******************************************************************************/
static void seg_add(line_t* l, uint16_t color, const char* fmt, const char* s)
{
    if (l->n < MAX_SEGS)
    {
        char tmp[TMP_LEN];

        /* every segment goes through the HUD charset (upper case ASCII) */
        (void)snprintf(tmp, sizeof(tmp), fmt, s);
        (void)gl_text_to_hud(tmp, sizeof(tmp), l->s[l->n].text, SEG_LEN);
        l->s[l->n].color = color;
        l->n++;
    }
}

static void seg_txt(line_t* l, uint16_t color, const char* s)
{
    seg_add(l, color, "%s", s);
}

/* 1e-7 deg -> "25.0331N" (4 decimals, about 11 m) */
static void fmt_coord(char* buf, size_t cap, int32_t v_e7, char pos, char neg)
{
    uint32_t a = (v_e7 < 0) ? (uint32_t)(-(int64_t)v_e7) : (uint32_t)v_e7;
    uint32_t deg = a / 10000000UL;
    uint32_t frac = (a % 10000000UL) / 1000UL;

    (void)snprintf(buf, cap, "%lu.%04lu%c", (unsigned long)deg, (unsigned long)frac,
                   (v_e7 < 0) ? neg : pos);
}

static uint32_t age_ms(const gl_down_t* d, uint32_t rx_ms)
{
    return d->cm33_tick_ms - rx_ms;
}

/* Up to GL_HUD_TEAM_ROWS member indexes: SOS first, then nearest */
static uint32_t team_order(const gl_team_t* t, uint8_t* idx)
{
    uint32_t n = 0U;
    bool used[GL_TEAM_MAX] = { false };

    while ((n < GL_HUD_TEAM_ROWS) && (n < t->count))
    {
        int32_t best = -1;

        for (uint32_t i = 0U; i < t->count; i++)
        {
            if (used[i])
            {
                continue;
            }
            if (best < 0)
            {
                best = (int32_t)i;
                continue;
            }
            {
                const gl_member_t* a = &t->m[i];
                const gl_member_t* b = &t->m[best];
                bool a_sos = (0U != (a->flags & GL_MEMBER_FLAG_SOS));
                bool b_sos = (0U != (b->flags & GL_MEMBER_FLAG_SOS));

                if ((a_sos && !b_sos) || ((a_sos == b_sos) && (a->distance_m < b->distance_m)))
                {
                    best = (int32_t)i;
                }
            }
        }
        used[best] = true;
        idx[n++] = (uint8_t)best;
    }
    return n;
}

static void build_line(const gl_down_t* d, uint32_t line, line_t* l)
{
    char buf[TMP_LEN];

    memset(l, 0, sizeof(*l));

    if (NULL == d)
    {
        if (0U == line)
        {
            seg_txt(l, COLOR_CYAN, "LINK ");
            seg_txt(l, COLOR_DIM, "CM33 OFFLINE");
        }
        return;
    }

    switch (line)
    {
        case 0U:
            if (0U != (d->link_mask & GL_LINK_PAIRING))
            {
                (void)snprintf(buf, sizeof(buf), "%06lu", (unsigned long)(d->passkey % 1000000UL));
                seg_txt(l, COLOR_YELLOW, "PAIR CODE ");
                seg_txt(l, COLOR_WHITE, buf);
                break;
            }
            seg_txt(l, COLOR_CYAN, "LINK ");
            if (0U == (d->link_mask & GL_LINK_BT_READY))
            {
                seg_txt(l, COLOR_RED, "BT DOWN");
                break;
            }
            seg_txt(l, (0U != (d->link_mask & GL_LINK_TRACKER)) ? COLOR_GREEN : COLOR_DIM, "TRK ");
            seg_txt(l, (0U != (d->link_mask & GL_LINK_PHONE)) ? COLOR_GREEN : COLOR_DIM, "PH ");
            seg_txt(l, (GL_WIFI_CONNECTED == d->wifi_state) ? COLOR_GREEN :
                       ((GL_WIFI_CONNECTING == d->wifi_state) ? COLOR_YELLOW :
                        ((GL_WIFI_FAILED == d->wifi_state) ? COLOR_RED : COLOR_DIM)), "WIFI");
            break;

        case 1U:
            seg_txt(l, COLOR_LABEL, "GPS ");
            if (!d->gnss_valid)
            {
                seg_txt(l, COLOR_DIM, "--");
            }
            else if (age_ms(d, d->gnss_rx_ms) > GL_HUD_GNSS_STALE_MS)
            {
                (void)snprintf(buf, sizeof(buf), "LOST %luS",
                               (unsigned long)(age_ms(d, d->gnss_rx_ms) / 1000U));
                seg_txt(l, COLOR_RED, buf);
            }
            else if (0U == (d->gnss.flags & GL_GNSS_FLAG_FIX))
            {
                seg_txt(l, COLOR_YELLOW, "NO FIX");
            }
            else
            {
                (void)snprintf(buf, sizeof(buf), "%s %02uSAT",
                               (0U != (d->gnss.flags & GL_GNSS_FLAG_3D)) ? "3D" : "2D",
                               (unsigned)d->gnss.sats);
                seg_txt(l, COLOR_GREEN, buf);
            }
            break;

        case 2U:
            if (d->gnss_valid && (0U != (d->gnss.flags & GL_GNSS_FLAG_FIX)))
            {
                char lat[12];
                char lon[13];
                fmt_coord(lat, sizeof(lat), d->gnss.lat_e7, 'N', 'S');
                fmt_coord(lon, sizeof(lon), d->gnss.lon_e7, 'E', 'W');
                (void)snprintf(buf, sizeof(buf), "%s %s", lat, lon);
                seg_txt(l, COLOR_WHITE, buf);
            }
            break;

        case 3U:
            if (d->gnss_valid && (0U != (d->gnss.flags & GL_GNSS_FLAG_FIX)))
            {
                /* cm/s -> km/h: x 0.036, rounded */
                unsigned kmh = (unsigned)(((uint32_t)d->gnss.speed_cms * 36U + 500U) / 1000U);
                long alt_m = (long)(d->gnss.alt_dm / 10);

                (void)snprintf(buf, sizeof(buf), "%ldM ", alt_m);
                seg_txt(l, COLOR_LABEL, "ALT ");
                seg_txt(l, COLOR_WHITE, buf);
                (void)snprintf(buf, sizeof(buf), "%uKMH %s", kmh, gl_compass8(d->gnss.course_cdeg));
                seg_txt(l, COLOR_WHITE, buf);
            }
            break;

        case 4U:
        {
            int32_t sos = -1;
            (void)snprintf(buf, sizeof(buf), "%u ", (unsigned)d->team.count);
            seg_txt(l, COLOR_LABEL, "TEAM ");
            seg_txt(l, COLOR_WHITE, buf);
            for (uint32_t i = 0U; i < d->team.count; i++)
            {
                if (0U != (d->team.m[i].flags & GL_MEMBER_FLAG_SOS))
                {
                    sos = (int32_t)i;
                    break;
                }
            }
            if (sos >= 0)
            {
                (void)snprintf(buf, sizeof(buf), " SOS %s", d->team.m[sos].name);
                seg_txt(l, COLOR_RED, buf);
            }
            else if ((d->team.count > 0U) &&
                     (age_ms(d, d->team_rx_ms) > GL_HUD_TEAM_STALE_MS))
            {
                seg_txt(l, COLOR_YELLOW, " OLD");
            }
            break;
        }

        case 5U:
        case 6U:
        {
            uint8_t idx[GL_HUD_TEAM_ROWS];
            uint32_t n = team_order(&d->team, idx);
            uint32_t row = line - 5U;

            if (row < n)
            {
                const gl_member_t* m = &d->team.m[idx[row]];
                char dist[8];
                bool sos = (0U != (m->flags & GL_MEMBER_FLAG_SOS));
                bool stale = (0U != (m->flags & GL_MEMBER_FLAG_STALE));
                uint16_t c = sos ? COLOR_RED : (stale ? COLOR_DIM : COLOR_WHITE);

                gl_format_distance(dist, sizeof(dist), m->distance_m);
                (void)snprintf(buf, sizeof(buf), "%-4s %-2s %-5s %+dM", m->name,
                               gl_compass8(m->bearing_cdeg), dist, (int)m->dalt_m);
                seg_txt(l, c, buf);
                if (sos)
                {
                    seg_txt(l, COLOR_RED, "!");
                }
                else if (0U != (m->flags & GL_MEMBER_FLAG_PTT))
                {
                    seg_txt(l, COLOR_CYAN, "<");   /* talking (PTT) */
                }
            }
            break;
        }

        case 7U:
            if (d->msg_valid &&
                ((0U == d->msg.ttl_s) || (age_ms(d, d->msg_rx_ms) < ((uint32_t)d->msg.ttl_s * 1000U))))
            {
                char txt[GL_HUD_COLS + 1U];
                uint16_t c = (GL_MSG_SOS == d->msg.type) ? COLOR_RED :
                             ((GL_MSG_AI == d->msg.type) ? COLOR_CYAN :
                              ((GL_MSG_NAV == d->msg.type) ? COLOR_GREEN : COLOR_WHITE));
                const char* label = (GL_MSG_SOS == d->msg.type) ? "SOS " :
                                    ((GL_MSG_AI == d->msg.type) ? "AI " :
                                     ((GL_MSG_NAV == d->msg.type) ? "NAV " : "MSG "));

                (void)gl_text_to_hud(d->msg.text, d->msg.len, txt,
                                     (GL_HUD_COLS + 1U) - strlen(label));
                seg_txt(l, COLOR_LABEL, label);
                seg_txt(l, c, txt);
            }
            break;

        default:
            break;
    }
}

/*******************************************************************************
* Rendering
*******************************************************************************/
typedef struct
{
    uint16_t* fb;
    uint32_t  stride;
    uint32_t  height;
} surface_t;

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
                          const char* text, uint16_t fg, uint32_t max_chars)
{
    const uint32_t sc = GL_HUD_SCALE;

    for (uint32_t n = 0U; ('\0' != *text) && (n < max_chars); text++, n++)
    {
        const uint8_t* g = find_glyph(*text);

        if (NULL != g)
        {
            for (uint32_t row = 0U; row < FONT_H; row++)
            {
                for (uint32_t col = 0U; col < FONT_W; col++)
                {
                    if (0U != (g[row] & (0x10U >> col)))
                    {
                        fill_rect(s, x + (col * sc), y + (row * sc), sc, sc, fg);
                    }
                }
            }
        }
        x += GL_HUD_CHAR_ADV;
    }
    return x;
}

void gl_hud_draw(uint16_t* fb, uint32_t stride, uint32_t fb_height,
                 uint32_t x, uint32_t y, const gl_down_t* d)
{
    surface_t s = { fb, stride, fb_height };

    if (NULL == fb)
    {
        return;
    }

    /* background + 1 px frame */
    fill_rect(&s, x, y, GL_HUD_WIDTH, GL_HUD_HEIGHT, COLOR_BG);
    fill_rect(&s, x, y, GL_HUD_WIDTH, 1U, COLOR_FRAME);
    fill_rect(&s, x, y + GL_HUD_HEIGHT - 1U, GL_HUD_WIDTH, 1U, COLOR_FRAME);
    fill_rect(&s, x, y, 1U, GL_HUD_HEIGHT, COLOR_FRAME);
    fill_rect(&s, x + GL_HUD_WIDTH - 1U, y, 1U, GL_HUD_HEIGHT, COLOR_FRAME);

    for (uint32_t line = 0U; line < GL_HUD_LINES; line++)
    {
        line_t l;
        uint32_t cx = x + GL_HUD_PAD;
        uint32_t cy = y + GL_HUD_PAD + (line * GL_HUD_LINE_ADV);
        uint32_t cols_left = GL_HUD_COLS;

        build_line(d, line, &l);
        for (uint32_t i = 0U; (i < l.n) && (cols_left > 0U); i++)
        {
            uint32_t len = (uint32_t)strlen(l.s[i].text);
            if (len > cols_left)
            {
                len = cols_left;
            }
            cx = draw_text(&s, cx, cy, l.s[i].text, l.s[i].color, len);
            cols_left -= len;
        }
    }
}

bool gl_hud_line_text(const gl_down_t* d, uint32_t line, char* out, uint32_t cap)
{
    line_t l;
    uint32_t o = 0U;

    if ((line >= GL_HUD_LINES) || (NULL == out) || (0U == cap))
    {
        return false;
    }

    build_line(d, line, &l);
    out[0] = '\0';
    for (uint32_t i = 0U; i < l.n; i++)
    {
        for (const char* p = l.s[i].text; ('\0' != *p) && (o < GL_HUD_COLS) && ((o + 1U) < cap); p++)
        {
            out[o++] = *p;
        }
    }
    out[o] = '\0';
    return true;
}
