/*******************************************************************************
* File Name        : goggle_link_proto.c
*
* Description      : See goggle_link_proto.h.
*
*******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "goggle_link_proto.h"

#define LAT_E7_MAX      (900000000L)
#define LON_E7_MAX      (1800000000L)
#define CDEG_FULL       (36000U)

/*******************************************************************************
* Little-endian helpers
*******************************************************************************/
static uint16_t rd_u16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_u16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)(v >> 8);
}

static void wr_u32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)(v >> 24);
}

static gl_result_t check_header(const uint8_t* p, size_t len, size_t min_len)
{
    if ((NULL == p) || (len < min_len))
    {
        return GL_ERR_LEN;
    }
    if (GL_PROTO_VERSION != p[0])
    {
        return GL_ERR_VERSION;
    }
    return GL_OK;
}

/*******************************************************************************
* Decoders
*******************************************************************************/
gl_result_t gl_gnss_decode(const uint8_t* p, size_t len, gl_gnss_t* out)
{
    gl_gnss_t g;
    gl_result_t r = check_header(p, len, GL_GNSS_LEN);

    if (GL_OK != r)
    {
        return r;
    }
    if (GL_GNSS_LEN != len)
    {
        return GL_ERR_LEN;
    }

    g.flags       = p[1];
    g.sats        = p[2];
    g.hdop_x10    = p[3];
    g.lat_e7      = (int32_t)rd_u32(&p[4]);
    g.lon_e7      = (int32_t)rd_u32(&p[8]);
    g.alt_dm      = (int32_t)rd_u32(&p[12]);
    g.speed_cms   = rd_u16(&p[16]);
    g.course_cdeg = rd_u16(&p[18]);
    g.utc_s       = rd_u32(&p[20]);

    if ((g.lat_e7 > LAT_E7_MAX) || (g.lat_e7 < -LAT_E7_MAX) ||
        (g.lon_e7 > LON_E7_MAX) || (g.lon_e7 < -LON_E7_MAX) ||
        (g.course_cdeg >= CDEG_FULL))
    {
        return GL_ERR_RANGE;
    }

    *out = g;
    return GL_OK;
}

gl_result_t gl_team_decode(const uint8_t* p, size_t len, gl_team_t* out)
{
    gl_team_t t;
    gl_result_t r = check_header(p, len, GL_TEAM_HDR_LEN);

    if (GL_OK != r)
    {
        return r;
    }

    t.count = p[1];
    if (t.count > GL_TEAM_MAX)
    {
        return GL_ERR_RANGE;
    }
    if (len != (GL_TEAM_HDR_LEN + ((size_t)t.count * GL_TEAM_ENTRY_LEN)))
    {
        return GL_ERR_LEN;
    }

    for (uint8_t i = 0U; i < t.count; i++)
    {
        const uint8_t* e = &p[GL_TEAM_HDR_LEN + ((size_t)i * GL_TEAM_ENTRY_LEN)];
        gl_member_t* m = &t.m[i];

        m->id           = e[0];
        m->flags        = e[1];
        m->battery_pct  = e[2];
        m->rssi_dbm     = (int8_t)e[3];
        m->bearing_cdeg = rd_u16(&e[4]);
        m->distance_m   = rd_u16(&e[6]);
        m->dalt_m       = (int16_t)rd_u16(&e[8]);
        m->age_s        = rd_u16(&e[10]);

        if (m->bearing_cdeg >= CDEG_FULL)
        {
            return GL_ERR_RANGE;
        }

        for (uint32_t k = 0U; k < GL_NAME_LEN; k++)
        {
            char c = (char)e[12U + k];
            /* stop at NUL, replace control / non-ASCII bytes */
            if ('\0' == c)
            {
                m->name[k] = '\0';
                break;
            }
            m->name[k] = ((c < 0x20) || ((uint8_t)c > 0x7EU)) ? '?' : c;
        }
        m->name[GL_NAME_LEN] = '\0';
    }

    /* unused slots stay deterministic */
    if (t.count < GL_TEAM_MAX)
    {
        memset(&t.m[t.count], 0, sizeof(gl_member_t) * (GL_TEAM_MAX - t.count));
    }

    *out = t;
    return GL_OK;
}

gl_result_t gl_msg_decode(const uint8_t* p, size_t len, gl_msg_t* out)
{
    size_t text_len;
    gl_result_t r = check_header(p, len, GL_MSG_HDR_LEN);

    if (GL_OK != r)
    {
        return r;
    }

    text_len = len - GL_MSG_HDR_LEN;
    if (text_len > GL_MSG_TEXT_MAX)
    {
        return GL_ERR_LEN;
    }
    if (p[1] >= (uint8_t)GL_MSG_TYPE_COUNT)
    {
        return GL_ERR_RANGE;
    }

    out->type     = p[1];
    out->from_id  = p[2];
    out->priority = p[3];
    out->ttl_s    = rd_u16(&p[4]);
    out->len      = (uint8_t)text_len;
    memcpy(out->text, &p[GL_MSG_HDR_LEN], text_len);
    out->text[text_len] = '\0';
    return GL_OK;
}

gl_result_t gl_ctrl_decode(const uint8_t* p, size_t len, gl_ctrl_t* out)
{
    gl_ctrl_t c;
    gl_result_t r = check_header(p, len, 2U);

    if (GL_OK != r)
    {
        return r;
    }

    memset(&c, 0, sizeof(c));
    c.cmd = p[1];

    switch (c.cmd)
    {
        case GL_CMD_HELLO:
            if (3U != len)
            {
                return GL_ERR_LEN;
            }
            if (p[2] > (uint8_t)GL_ROLE_PHONE)
            {
                return GL_ERR_RANGE;
            }
            c.role = p[2];
            break;

        case GL_CMD_SET_TIME:
            if (7U != len)
            {
                return GL_ERR_LEN;
            }
            c.utc_s  = rd_u32(&p[2]);
            c.tz_q15 = (int8_t)p[6];
            /* UTC-12:00 .. UTC+14:00 */
            if ((c.tz_q15 < -48) || (c.tz_q15 > 56))
            {
                return GL_ERR_RANGE;
            }
            break;

        case GL_CMD_BRIGHTNESS:
            if (3U != len)
            {
                return GL_ERR_LEN;
            }
            if (p[2] > 100U)
            {
                return GL_ERR_RANGE;
            }
            c.brightness = p[2];
            break;

        case GL_CMD_WIFI:
            if (3U != len)
            {
                return GL_ERR_LEN;
            }
            if (p[2] > 1U)
            {
                return GL_ERR_RANGE;
            }
            c.wifi_on = p[2];
            break;

        case GL_CMD_CLEAR_MSG:
            if (2U != len)
            {
                return GL_ERR_LEN;
            }
            break;

        default:
            return GL_ERR_RANGE;
    }

    *out = c;
    return GL_OK;
}

gl_result_t gl_wifi_cred_decode(const uint8_t* p, size_t len, gl_wifi_cred_t* out)
{
    size_t ssid_len;
    size_t pass_len;
    gl_result_t r = check_header(p, len, 3U);

    if (GL_OK != r)
    {
        return r;
    }

    ssid_len = p[1];
    if ((0U == ssid_len) || (ssid_len > GL_WIFI_SSID_MAX))
    {
        return GL_ERR_RANGE;
    }
    if (len < (3U + ssid_len))
    {
        return GL_ERR_LEN;
    }
    pass_len = p[2U + ssid_len];
    if (pass_len > GL_WIFI_PASS_MAX)
    {
        return GL_ERR_RANGE;
    }
    if (len != (3U + ssid_len + pass_len))
    {
        return GL_ERR_LEN;
    }

    memset(out, 0, sizeof(*out));
    out->ssid_len = (uint8_t)ssid_len;
    out->pass_len = (uint8_t)pass_len;
    memcpy(out->ssid, &p[2], ssid_len);
    memcpy(out->pass, &p[3U + ssid_len], pass_len);
    return GL_OK;
}

gl_result_t gl_status_decode(const uint8_t* p, size_t len, gl_status_t* out)
{
    gl_result_t r = check_header(p, len, GL_STATUS_LEN);

    if (GL_OK != r)
    {
        return r;
    }
    if (GL_STATUS_LEN != len)
    {
        return GL_ERR_LEN;
    }

    out->battery_pct  = p[1];
    out->flags        = p[2];
    out->link_mask    = p[3];
    out->speed_cms    = rd_u16(&p[4]);
    out->heading_cdeg = rd_u16(&p[6]);
    out->alt_dm       = (int32_t)rd_u32(&p[8]);
    out->temp_c10     = (int16_t)rd_u16(&p[12]);
    return GL_OK;
}

/*******************************************************************************
* Encoders
*******************************************************************************/
size_t gl_status_encode(const gl_status_t* in, uint8_t* p, size_t cap)
{
    if ((NULL == in) || (NULL == p) || (cap < GL_STATUS_LEN))
    {
        return 0U;
    }
    p[0] = GL_PROTO_VERSION;
    p[1] = in->battery_pct;
    p[2] = in->flags;
    p[3] = in->link_mask;
    wr_u16(&p[4], in->speed_cms);
    wr_u16(&p[6], in->heading_cdeg);
    wr_u32(&p[8], (uint32_t)in->alt_dm);
    wr_u16(&p[12], (uint16_t)in->temp_c10);
    wr_u16(&p[14], 0U);
    return GL_STATUS_LEN;
}

size_t gl_gnss_encode(const gl_gnss_t* in, uint8_t* p, size_t cap)
{
    if ((NULL == in) || (NULL == p) || (cap < GL_GNSS_LEN))
    {
        return 0U;
    }
    p[0] = GL_PROTO_VERSION;
    p[1] = in->flags;
    p[2] = in->sats;
    p[3] = in->hdop_x10;
    wr_u32(&p[4], (uint32_t)in->lat_e7);
    wr_u32(&p[8], (uint32_t)in->lon_e7);
    wr_u32(&p[12], (uint32_t)in->alt_dm);
    wr_u16(&p[16], in->speed_cms);
    wr_u16(&p[18], in->course_cdeg);
    wr_u32(&p[20], in->utc_s);
    return GL_GNSS_LEN;
}

size_t gl_team_encode(const gl_team_t* in, uint8_t* p, size_t cap)
{
    size_t need;

    if ((NULL == in) || (NULL == p) || (in->count > GL_TEAM_MAX))
    {
        return 0U;
    }
    need = GL_TEAM_HDR_LEN + ((size_t)in->count * GL_TEAM_ENTRY_LEN);
    if (cap < need)
    {
        return 0U;
    }

    p[0] = GL_PROTO_VERSION;
    p[1] = in->count;
    for (uint8_t i = 0U; i < in->count; i++)
    {
        uint8_t* e = &p[GL_TEAM_HDR_LEN + ((size_t)i * GL_TEAM_ENTRY_LEN)];
        const gl_member_t* m = &in->m[i];

        e[0] = m->id;
        e[1] = m->flags;
        e[2] = m->battery_pct;
        e[3] = (uint8_t)m->rssi_dbm;
        wr_u16(&e[4], m->bearing_cdeg);
        wr_u16(&e[6], m->distance_m);
        wr_u16(&e[8], (uint16_t)m->dalt_m);
        wr_u16(&e[10], m->age_s);
        memset(&e[12], 0, GL_NAME_LEN);
        for (uint32_t k = 0U; (k < GL_NAME_LEN) && ('\0' != m->name[k]); k++)
        {
            e[12U + k] = (uint8_t)m->name[k];
        }
    }
    return need;
}

size_t gl_msg_encode(const gl_msg_t* in, uint8_t* p, size_t cap)
{
    size_t need;

    if ((NULL == in) || (NULL == p) || (in->len > GL_MSG_TEXT_MAX))
    {
        return 0U;
    }
    need = GL_MSG_HDR_LEN + in->len;
    if (cap < need)
    {
        return 0U;
    }
    p[0] = GL_PROTO_VERSION;
    p[1] = in->type;
    p[2] = in->from_id;
    p[3] = in->priority;
    wr_u16(&p[4], in->ttl_s);
    memcpy(&p[GL_MSG_HDR_LEN], in->text, in->len);
    return need;
}

/*******************************************************************************
* HUD helpers
*******************************************************************************/
const char* gl_compass8(uint16_t bearing_cdeg)
{
    static const char* const names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    /* sectors of 45 deg centred on each direction */
    uint32_t idx = (((uint32_t)(bearing_cdeg % CDEG_FULL) + 2250U) / 4500U) % 8U;
    return names[idx];
}

uint16_t gl_relative_bearing(uint16_t bearing_cdeg, uint16_t heading_cdeg)
{
    int32_t d = (int32_t)(bearing_cdeg % CDEG_FULL) - (int32_t)(heading_cdeg % CDEG_FULL);
    if (d < 0)
    {
        d += (int32_t)CDEG_FULL;
    }
    return (uint16_t)d;
}

void gl_format_distance(char* buf, size_t cap, uint16_t distance_m)
{
    if ((NULL == buf) || (0U == cap))
    {
        return;
    }
    if (0xFFFFU == distance_m)
    {
        (void)snprintf(buf, cap, ">65KM");
    }
    else if (distance_m < 1000U)
    {
        (void)snprintf(buf, cap, "%uM", (unsigned)distance_m);
    }
    else if (distance_m < 10000U)
    {
        /* one decimal, rounded: 1250 -> 1.3KM */
        unsigned hm = ((unsigned)distance_m + 50U) / 100U;
        (void)snprintf(buf, cap, "%u.%uKM", hm / 10U, hm % 10U);
    }
    else
    {
        (void)snprintf(buf, cap, "%uKM", ((unsigned)distance_m + 500U) / 1000U);
    }
}

size_t gl_text_to_hud(const char* in, size_t in_len, char* out, size_t cap)
{
    size_t o = 0U;
    size_t i = 0U;

    if ((NULL == out) || (0U == cap))
    {
        return 0U;
    }

    while ((NULL != in) && (i < in_len) && ('\0' != in[i]) && ((o + 1U) < cap))
    {
        uint8_t c = (uint8_t)in[i];

        if (c >= 0x80U)
        {
            /* skip the whole UTF-8 sequence, emit one '?' */
            size_t n = (c >= 0xF0U) ? 4U : ((c >= 0xE0U) ? 3U : ((c >= 0xC0U) ? 2U : 1U));
            i += n;
            out[o++] = '?';
            continue;
        }
        if ((c >= (uint8_t)'a') && (c <= (uint8_t)'z'))
        {
            c = (uint8_t)(c - 0x20U);
        }
        out[o++] = ((c < 0x20U) || (c > 0x5FU)) ? '?' : (char)c;
        i++;
    }
    out[o] = '\0';
    return o;
}
