/*******************************************************************************
* File Name        : test_goggle_link.c
*
* Description      : Host unit test (no ModusToolbox) of the HUD Link packet
*                    codec, the shared-memory mailbox / log ring and the LINK
*                    panel renderer. Optionally writes the panel as a PPM
*                    image: ./test_goggle_link preview.ppm
*
*******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "goggle_link_proto.h"
#include "goggle_link_shm.h"
#include "goggle_link_hud.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_STR(a, b) do { checks++; if (0 != strcmp((a), (b))) { failures++; \
    printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

/*******************************************************************************
* Codec
*******************************************************************************/
static gl_gnss_t sample_gnss(void)
{
    gl_gnss_t g;
    memset(&g, 0, sizeof(g));
    g.flags       = GL_GNSS_FLAG_FIX | GL_GNSS_FLAG_3D;
    g.sats        = 9U;
    g.hdop_x10    = 12U;
    g.lat_e7      = 360812345;      /* 36.0812345 N (Niseko-ish) */
    g.lon_e7      = 1406612345;     /* 140.6612345 E */
    g.alt_dm      = 12345;          /* 1234.5 m */
    g.speed_cms   = 1250U;          /* 45 km/h */
    g.course_cdeg = 4500U;          /* NE */
    g.utc_s       = 1795000000UL;
    return g;
}

static gl_team_t sample_team(void)
{
    gl_team_t t;
    memset(&t, 0, sizeof(t));
    t.count = 3U;
    t.m[0] = (gl_member_t){ .id = 2, .flags = 0, .battery_pct = 80, .rssi_dbm = -95,
                            .bearing_cdeg = 22000, .distance_m = 1240, .dalt_m = -40,
                            .age_s = 3, .name = "bob" };
    t.m[1] = (gl_member_t){ .id = 3, .flags = GL_MEMBER_FLAG_SOS, .battery_pct = 15,
                            .rssi_dbm = -110, .bearing_cdeg = 4000, .distance_m = 2600,
                            .dalt_m = 85, .age_s = 5, .name = "ANNA" };
    t.m[2] = (gl_member_t){ .id = 4, .flags = GL_MEMBER_FLAG_PTT, .battery_pct = 55,
                            .rssi_dbm = -80, .bearing_cdeg = 100, .distance_m = 320,
                            .dalt_m = 12, .age_s = 1, .name = "KEN" };
    return t;
}

static void test_gnss(void)
{
    uint8_t buf[64];
    gl_gnss_t in = sample_gnss();
    gl_gnss_t out;
    size_t n = gl_gnss_encode(&in, buf, sizeof(buf));

    CHECK(GL_GNSS_LEN == n);
    CHECK(GL_OK == gl_gnss_decode(buf, n, &out));
    CHECK(out.lat_e7 == in.lat_e7);
    CHECK(out.lon_e7 == in.lon_e7);
    CHECK(out.alt_dm == in.alt_dm);
    CHECK(out.speed_cms == in.speed_cms);
    CHECK(out.course_cdeg == in.course_cdeg);
    CHECK(out.utc_s == in.utc_s);
    CHECK(out.sats == 9U);

    /* little endian on the wire: lat 360812345 = 0x15818F39 */
    CHECK((buf[4] == 0x39) && (buf[5] == 0x8F) && (buf[6] == 0x81) && (buf[7] == 0x15));

    /* negative coordinates survive */
    in.lat_e7 = -338688000;
    in.lon_e7 = -706000000;
    in.alt_dm = -150;
    n = gl_gnss_encode(&in, buf, sizeof(buf));
    CHECK(GL_OK == gl_gnss_decode(buf, n, &out));
    CHECK((out.lat_e7 == -338688000) && (out.lon_e7 == -706000000) && (out.alt_dm == -150));

    /* errors */
    CHECK(GL_ERR_LEN == gl_gnss_decode(buf, n - 1U, &out));
    CHECK(GL_ERR_LEN == gl_gnss_decode(buf, n + 1U, &out));
    buf[0] = 2U;
    CHECK(GL_ERR_VERSION == gl_gnss_decode(buf, n, &out));
    in.lat_e7 = 900000001;
    n = gl_gnss_encode(&in, buf, sizeof(buf));
    CHECK(GL_ERR_RANGE == gl_gnss_decode(buf, n, &out));
    in = sample_gnss();
    in.course_cdeg = 36000U;
    n = gl_gnss_encode(&in, buf, sizeof(buf));
    CHECK(GL_ERR_RANGE == gl_gnss_decode(buf, n, &out));
    CHECK(0U == gl_gnss_encode(&in, buf, GL_GNSS_LEN - 1U));
}

static void test_team(void)
{
    uint8_t buf[GL_TEAM_MAX_LEN + 8U];
    gl_team_t in = sample_team();
    gl_team_t out;
    size_t n = gl_team_encode(&in, buf, sizeof(buf));

    CHECK((GL_TEAM_HDR_LEN + 3U * GL_TEAM_ENTRY_LEN) == n);
    CHECK(GL_OK == gl_team_decode(buf, n, &out));
    CHECK(3U == out.count);
    CHECK_STR(out.m[0].name, "bob");
    CHECK_STR(out.m[1].name, "ANNA");
    CHECK(out.m[1].flags == GL_MEMBER_FLAG_SOS);
    CHECK(out.m[0].dalt_m == -40);
    CHECK(out.m[1].rssi_dbm == -110);
    CHECK(out.m[0].distance_m == 1240U);
    CHECK(0U == out.m[5].id);       /* unused slots cleared */

    /* full team of 8 = 130 bytes: needs MTU >= 133 */
    in.count = GL_TEAM_MAX;
    n = gl_team_encode(&in, buf, sizeof(buf));
    CHECK(130U == n);
    CHECK(GL_OK == gl_team_decode(buf, n, &out));

    /* errors */
    buf[1] = 9U;
    CHECK(GL_ERR_RANGE == gl_team_decode(buf, n, &out));
    buf[1] = 2U;
    CHECK(GL_ERR_LEN == gl_team_decode(buf, n, &out));
    buf[1] = 0U;
    CHECK(GL_OK == gl_team_decode(buf, 2U, &out));
    CHECK(0U == out.count);

    /* non-printable name bytes are replaced */
    in = sample_team();
    in.count = 1U;
    n = gl_team_encode(&in, buf, sizeof(buf));
    buf[GL_TEAM_HDR_LEN + 12U] = 0x01;
    buf[GL_TEAM_HDR_LEN + 13U] = 0xE4;
    CHECK(GL_OK == gl_team_decode(buf, n, &out));
    CHECK_STR(out.m[0].name, "??b");
}

static void test_msg(void)
{
    uint8_t buf[GL_MSG_MAX_LEN + 8U];
    gl_msg_t in;
    gl_msg_t out;
    size_t n;

    memset(&in, 0, sizeof(in));
    in.type = GL_MSG_TEXT;
    in.from_id = 3U;
    in.ttl_s = 30U;
    strcpy(in.text, "Meet at lift 3");
    in.len = (uint8_t)strlen(in.text);

    n = gl_msg_encode(&in, buf, sizeof(buf));
    CHECK((GL_MSG_HDR_LEN + 14U) == n);
    CHECK(GL_OK == gl_msg_decode(buf, n, &out));
    CHECK_STR(out.text, "Meet at lift 3");
    CHECK(30U == out.ttl_s);

    /* empty text is allowed, 97 bytes is not */
    CHECK(GL_OK == gl_msg_decode(buf, GL_MSG_HDR_LEN, &out));
    CHECK(0U == out.len);
    memset(buf + GL_MSG_HDR_LEN, 'A', GL_MSG_TEXT_MAX + 1U);
    CHECK(GL_ERR_LEN == gl_msg_decode(buf, GL_MSG_MAX_LEN + 1U, &out));
    CHECK(GL_OK == gl_msg_decode(buf, GL_MSG_MAX_LEN, &out));
    CHECK(GL_MSG_TEXT_MAX == out.len);

    buf[1] = (uint8_t)GL_MSG_TYPE_COUNT;
    CHECK(GL_ERR_RANGE == gl_msg_decode(buf, GL_MSG_HDR_LEN, &out));
}

static void test_ctrl_wifi_status(void)
{
    gl_ctrl_t c;
    gl_wifi_cred_t w;
    gl_status_t s;
    gl_status_t s2;
    uint8_t buf[GL_WIFI_CRED_MAX_LEN + 4U];

    const uint8_t hello[] = { 1, GL_CMD_HELLO, GL_ROLE_TRACKER };
    const uint8_t time_[] = { 1, GL_CMD_SET_TIME, 0x00, 0x2F, 0xFD, 0x6A, 32 }; /* UTC+8 */
    const uint8_t bright[] = { 1, GL_CMD_BRIGHTNESS, 101 };
    const uint8_t wifi_on[] = { 1, GL_CMD_WIFI, 1 };
    const uint8_t clear[] = { 1, GL_CMD_CLEAR_MSG };
    const uint8_t bad_tz[] = { 1, GL_CMD_SET_TIME, 0, 0, 0, 0, 60 };
    const uint8_t unknown[] = { 1, 0x7F };

    CHECK(GL_OK == gl_ctrl_decode(hello, sizeof(hello), &c));
    CHECK((GL_CMD_HELLO == c.cmd) && (GL_ROLE_TRACKER == c.role));
    CHECK(GL_OK == gl_ctrl_decode(time_, sizeof(time_), &c));
    CHECK((0x6AFD2F00UL == c.utc_s) && (32 == c.tz_q15));
    CHECK(GL_ERR_RANGE == gl_ctrl_decode(bright, sizeof(bright), &c));
    CHECK(GL_OK == gl_ctrl_decode(wifi_on, sizeof(wifi_on), &c));
    CHECK(1U == c.wifi_on);
    CHECK(GL_OK == gl_ctrl_decode(clear, sizeof(clear), &c));
    CHECK(GL_ERR_LEN == gl_ctrl_decode(hello, 2U, &c));
    CHECK(GL_ERR_RANGE == gl_ctrl_decode(bad_tz, sizeof(bad_tz), &c));
    CHECK(GL_ERR_RANGE == gl_ctrl_decode(unknown, sizeof(unknown), &c));

    /* Wi-Fi credentials: ver, len, "Lodge", len, "secret123" */
    buf[0] = 1; buf[1] = 5; memcpy(&buf[2], "Lodge", 5);
    buf[7] = 9; memcpy(&buf[8], "secret123", 9);
    CHECK(GL_OK == gl_wifi_cred_decode(buf, 17U, &w));
    CHECK_STR(w.ssid, "Lodge");
    CHECK_STR(w.pass, "secret123");
    CHECK(GL_ERR_LEN == gl_wifi_cred_decode(buf, 16U, &w));
    buf[7] = 0;     /* open network */
    CHECK(GL_OK == gl_wifi_cred_decode(buf, 8U, &w));
    CHECK(0U == w.pass_len);
    buf[1] = 0;
    CHECK(GL_ERR_RANGE == gl_wifi_cred_decode(buf, 8U, &w));
    buf[1] = 33;
    CHECK(GL_ERR_RANGE == gl_wifi_cred_decode(buf, 8U, &w));

    /* status round trip */
    memset(&s, 0, sizeof(s));
    s.battery_pct = 76U;
    s.flags = GL_STATUS_FLAG_HEATER;
    s.link_mask = GL_LINK_TRACKER | GL_LINK_PHONE;
    s.speed_cms = 1500U;
    s.heading_cdeg = 27000U;
    s.alt_dm = 23456;
    s.temp_c10 = -125;
    CHECK(GL_STATUS_LEN == gl_status_encode(&s, buf, sizeof(buf)));
    CHECK(GL_OK == gl_status_decode(buf, GL_STATUS_LEN, &s2));
    CHECK((76U == s2.battery_pct) && (-125 == s2.temp_c10) && (23456 == s2.alt_dm) &&
          (27000U == s2.heading_cdeg) && (s2.link_mask == s.link_mask));
}

static void test_helpers(void)
{
    char b[16];

    CHECK_STR(gl_compass8(0U), "N");
    CHECK_STR(gl_compass8(2249U), "N");
    CHECK_STR(gl_compass8(2250U), "NE");
    CHECK_STR(gl_compass8(9000U), "E");
    CHECK_STR(gl_compass8(22000U), "SW");
    CHECK_STR(gl_compass8(33750U), "N");
    CHECK_STR(gl_compass8(35999U), "N");

    CHECK(9000U == gl_relative_bearing(18000U, 9000U));
    CHECK(27000U == gl_relative_bearing(0U, 9000U));

    gl_format_distance(b, sizeof(b), 0U);      CHECK_STR(b, "0M");
    gl_format_distance(b, sizeof(b), 999U);    CHECK_STR(b, "999M");
    gl_format_distance(b, sizeof(b), 1000U);   CHECK_STR(b, "1.0KM");
    gl_format_distance(b, sizeof(b), 1250U);   CHECK_STR(b, "1.3KM");
    gl_format_distance(b, sizeof(b), 9949U);   CHECK_STR(b, "9.9KM");
    gl_format_distance(b, sizeof(b), 12400U);  CHECK_STR(b, "12KM");
    gl_format_distance(b, sizeof(b), 0xFFFFU); CHECK_STR(b, ">65KM");

    /* "Hi 隊友!" -> "HI", one question mark per CJK character, "!" */
    CHECK(6U == gl_text_to_hud("Hi \xE9\x9A\x8A\xE5\x8F\x8B!", 10U, b, sizeof(b)));
    CHECK_STR(b, "HI ?" "?!");
    gl_text_to_hud("abc{}~", 6U, b, sizeof(b));
    CHECK_STR(b, "ABC???");
    gl_text_to_hud("truncate me", 11U, b, 6U);
    CHECK_STR(b, "TRUNC");
}

/*******************************************************************************
* Mailbox
*******************************************************************************/
static void set_gnss(gl_down_t* d, void* ctx)
{
    d->gnss = *(gl_gnss_t*)ctx;
    d->gnss_valid = true;
}

static void set_status(gl_up_t* u, void* ctx)
{
    u->status.battery_pct = *(uint8_t*)ctx;
}

static void test_shm(void)
{
    gl_down_t d;
    gl_up_t u;
    gl_gnss_t g = sample_gnss();
    uint8_t batt = 42U;
    char out[64];
    size_t n;

    CHECK(sizeof(gl_shm_t) <= GL_SHM_RESERVED);
    CHECK(0U == (offsetof(gl_shm_t, up) % GL_SHM_LINE));
    CHECK(0U == (offsetof(gl_shm_t, log_tail) % GL_SHM_LINE));
    CHECK(offsetof(gl_shm_t, log_tail) != offsetof(gl_shm_t, log_head));

    CHECK(!gl_shm_down_read(&d));      /* not initialised yet */
    gl_shm_init_owner();
    CHECK(gl_shm_ready());
    CHECK(gl_shm_down_read(&d));
    CHECK(0U == d.seq);
    CHECK(!d.gnss_valid);

    gl_shm_down_write(set_gnss, &g);
    CHECK(gl_shm_down_read(&d));
    CHECK(2U == d.seq);
    CHECK(d.gnss_valid && (d.gnss.lat_e7 == g.lat_e7));

    /* a reader that sees an odd sequence (writer in progress) gives up */
    gl_shm()->down.seq = 3U;
    CHECK(!gl_shm_down_read(&d));
    gl_shm()->down.seq = 4U;
    CHECK(gl_shm_down_read(&d));

    gl_shm_up_write(set_status, &batt);
    CHECK(gl_shm_up_read(&u));
    CHECK(42U == u.status.battery_pct);
    CHECK(0xFFFFU == u.status.heading_cdeg);

    /* log ring */
    CHECK(0U == gl_shm_log_pop(out, sizeof(out)));
    gl_shm_log_push("hello\r\n", 7U);
    gl_shm_log_push("world\r\n", 7U);
    n = gl_shm_log_pop(out, sizeof(out));
    CHECK(14U == n);
    CHECK(0 == memcmp(out, "hello\r\nworld\r\n", 14U));
    CHECK(0U == gl_shm_log_pop(out, sizeof(out)));

    /* fill the ring: lines that do not fit are dropped, never torn */
    {
        char line[100];
        size_t total = 0U;
        size_t got;
        char big[GL_LOG_RING_SIZE];

        memset(line, 'x', sizeof(line));
        for (int i = 0; i < 30; i++)
        {
            gl_shm_log_push(line, sizeof(line));
        }
        while ((got = gl_shm_log_pop(big, sizeof(big))) > 0U)
        {
            total += got;
        }
        CHECK(2000U == total);          /* 20 lines of 100 fit in 2047 */
        /* wrap-around keeps working */
        gl_shm_log_push("wrap\n", 5U);
        CHECK(5U == gl_shm_log_pop(out, sizeof(out)));
        CHECK(0 == memcmp(out, "wrap\n", 5U));
    }
}

/*******************************************************************************
* Panel
*******************************************************************************/
static void fill_demo(gl_down_t* d)
{
    memset(d, 0, sizeof(*d));
    d->magic        = GL_SHM_MAGIC;
    d->cm33_tick_ms = 100000U;
    d->link_mask    = GL_LINK_BT_READY | GL_LINK_TRACKER | GL_LINK_PHONE;
    d->wifi_state   = GL_WIFI_OFF;
    d->gnss_valid   = true;
    d->gnss         = sample_gnss();
    d->gnss_rx_ms   = 99500U;
    d->team         = sample_team();
    d->team_rx_ms   = 99000U;
    d->msg_valid    = true;
    d->msg_rx_ms    = 98000U;
    d->msg.type     = GL_MSG_TEXT;
    d->msg.ttl_s    = 30U;
    strcpy(d->msg.text, "Meet at lift 3");
    d->msg.len      = (uint8_t)strlen(d->msg.text);
}

static void test_hud(const char* ppm_path)
{
    gl_down_t d;
    char l[32];
    const uint32_t W = 300U;
    const uint32_t H = 200U;
    uint16_t* fb = calloc((size_t)W * H, sizeof(uint16_t));

    fill_demo(&d);

    gl_hud_line_text(&d, 0U, l, sizeof(l)); CHECK_STR(l, "LINK TRK PH WIFI");
    gl_hud_line_text(&d, 1U, l, sizeof(l)); CHECK_STR(l, "GPS 3D 09SAT");
    gl_hud_line_text(&d, 2U, l, sizeof(l)); CHECK_STR(l, "36.0812N 140.6612E");
    gl_hud_line_text(&d, 3U, l, sizeof(l)); CHECK_STR(l, "ALT 1234M 45KMH NE");
    gl_hud_line_text(&d, 4U, l, sizeof(l)); CHECK_STR(l, "TEAM 3  SOS ANNA");
    gl_hud_line_text(&d, 5U, l, sizeof(l)); CHECK_STR(l, "ANNA NE 2.6KM +85M!");
    gl_hud_line_text(&d, 6U, l, sizeof(l)); CHECK_STR(l, "KEN  N  320M  +12M<");
    gl_hud_line_text(&d, 7U, l, sizeof(l)); CHECK_STR(l, "MSG MEET AT LIFT 3");
    CHECK(!gl_hud_line_text(&d, 8U, l, sizeof(l)));

    if (NULL != ppm_path)
    {
        gl_hud_draw(fb, W, H, 10U, 10U, &d);
        FILE* f = fopen(ppm_path, "wb");
        if (NULL != f)
        {
            fprintf(f, "P6\n%u %u\n255\n", (unsigned)W, (unsigned)H);
            for (uint32_t i = 0U; i < W * H; i++)
            {
                uint16_t p = fb[i];
                uint8_t rgb[3] = { (uint8_t)((p >> 8) & 0xF8U), (uint8_t)((p >> 3) & 0xFCU),
                                   (uint8_t)((p << 3) & 0xF8U) };
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
        }
    }

    /* stale GNSS, message expired, pairing code */
    d.gnss_rx_ms = 88000U;
    gl_hud_line_text(&d, 1U, l, sizeof(l)); CHECK_STR(l, "GPS LOST 12S");
    d.msg_rx_ms = 60000U;
    gl_hud_line_text(&d, 7U, l, sizeof(l)); CHECK_STR(l, "");
    d.link_mask |= GL_LINK_PAIRING;
    d.passkey = 42U;
    gl_hud_line_text(&d, 0U, l, sizeof(l)); CHECK_STR(l, "PAIR CODE 000042");
    d.gnss.flags = 0U;
    d.gnss_rx_ms = 99900U;
    gl_hud_line_text(&d, 1U, l, sizeof(l)); CHECK_STR(l, "GPS NO FIX");
    gl_hud_line_text(&d, 2U, l, sizeof(l)); CHECK_STR(l, "");

    /* tick counter wrap-around */
    d.cm33_tick_ms = 1000U;
    d.gnss_rx_ms = 0xFFFFFF00U;
    d.gnss.flags = GL_GNSS_FLAG_FIX;
    gl_hud_line_text(&d, 1U, l, sizeof(l)); CHECK_STR(l, "GPS 2D 09SAT");

    gl_hud_line_text(NULL, 0U, l, sizeof(l)); CHECK_STR(l, "LINK CM33 OFFLINE");

    /* drawing never writes outside the buffer (panel clipped at the edge) */
    memset(fb, 0, (size_t)W * H * sizeof(uint16_t));
    fill_demo(&d);
    gl_hud_draw(fb, W, H, W - 100U, H - 50U, &d);
    gl_hud_draw(fb, W, H, W + 10U, H + 10U, &d);
    CHECK(GL_HUD_WIDTH == 256U);
    CHECK(GL_HUD_HEIGHT == 160U);

    free(fb);
}

int main(int argc, char** argv)
{
    test_gnss();
    test_team();
    test_msg();
    test_ctrl_wifi_status();
    test_helpers();
    test_shm();
    test_hud((argc > 1) ? argv[1] : NULL);

    printf("%d checks, %d failed\n", checks, failures);
    return (0 == failures) ? 0 : 1;
}
