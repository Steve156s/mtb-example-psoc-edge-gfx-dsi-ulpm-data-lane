/*******************************************************************************
* File Name        : goggle_link_proto.h
*
* Description      : Over-the-air format of the "HUD Link" GATT service that
*                    the goggle exposes to the LoRa Tracker and to the phone.
*                    Pure C, no SDK dependency: the same file is used by the
*                    CM33 Bluetooth code, the CM55 HUD and the host unit test.
*
*                    All multi-byte fields are little endian. Every packet
*                    starts with a version byte (GL_PROTO_VERSION).
*
*******************************************************************************/
#ifndef GOGGLE_LINK_PROTO_H
#define GOGGLE_LINK_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*******************************************************************************
* Service and characteristic UUIDs (128 bit, little endian byte order as used
* by the WICED GATT DB macros). Base: 4A5D0000-6F1E-4B8C-9C3A-1D2E5A0B7C60,
* the 16-bit field (bytes 12..13) selects the characteristic.
*******************************************************************************/
#define GL_UUID128(lo, hi)  0x60, 0x7C, 0x0B, 0x5A, 0x2E, 0x1D, 0x3A, 0x9C, \
                            0x8C, 0x4B, 0x1E, 0x6F, (lo), (hi), 0x5D, 0x4A

#define GL_UUID_SERVICE         GL_UUID128(0x00, 0x00)  /* HUD Link service       */
#define GL_UUID_TRACKER_GNSS    GL_UUID128(0x01, 0x00)  /* W/WNR  Tracker fix      */
#define GL_UUID_TEAM            GL_UUID128(0x02, 0x00)  /* W/WNR  team members     */
#define GL_UUID_MESSAGE         GL_UUID128(0x03, 0x00)  /* W      text / SOS / nav */
#define GL_UUID_GOGGLE_STATUS   GL_UUID128(0x04, 0x00)  /* R/N    goggle status    */
#define GL_UUID_CONTROL         GL_UUID128(0x05, 0x00)  /* W      commands         */
#define GL_UUID_WIFI_CRED       GL_UUID128(0x06, 0x00)  /* W enc. Wi-Fi SSID/pass  */

#define GL_PROTO_VERSION        (1U)

/*******************************************************************************
* Tracker GNSS fix (Tracker -> goggle), GL_GNSS_LEN bytes
*  0 ver | 1 flags | 2 sats | 3 hdop x10 | 4 lat 1e-7 deg (i32)
*  8 lon 1e-7 deg (i32) | 12 alt MSL dm (i32) | 16 speed cm/s (u16)
* 18 course 0.01 deg (u16) | 20 UTC Unix s (u32, 0 = unknown)
*******************************************************************************/
#define GL_GNSS_LEN             (24U)
#define GL_GNSS_FLAG_FIX        (0x01U)
#define GL_GNSS_FLAG_3D         (0x02U)

typedef struct
{
    uint8_t  flags;
    uint8_t  sats;
    uint8_t  hdop_x10;
    int32_t  lat_e7;
    int32_t  lon_e7;
    int32_t  alt_dm;
    uint16_t speed_cms;
    uint16_t course_cdeg;
    uint32_t utc_s;
} gl_gnss_t;

/*******************************************************************************
* Team members (Tracker -> goggle). The Tracker receives the others over LoRa
* and sends positions relative to its own fix, so the goggle needs no maths.
*  0 ver | 1 count (0..GL_TEAM_MAX) | count x GL_TEAM_ENTRY_LEN:
*  0 id | 1 flags | 2 battery % (0xFF unknown) | 3 LoRa RSSI dBm (i8)
*  4 bearing from me, true north, 0.01 deg (u16) | 6 distance m (u16,
*  0xFFFF = 65.5 km or more) | 8 altitude difference m (i16, + = above me)
* 10 age of the position s (u16) | 12 name, 4 ASCII chars, not terminated
*******************************************************************************/
#define GL_TEAM_MAX             (8U)
#define GL_TEAM_HDR_LEN         (2U)
#define GL_TEAM_ENTRY_LEN       (16U)
#define GL_TEAM_MAX_LEN         (GL_TEAM_HDR_LEN + (GL_TEAM_MAX * GL_TEAM_ENTRY_LEN))
#define GL_NAME_LEN             (4U)

#define GL_MEMBER_FLAG_SOS      (0x01U)
#define GL_MEMBER_FLAG_STALE    (0x02U)
#define GL_MEMBER_FLAG_LOW_BATT (0x04U)
#define GL_MEMBER_FLAG_PTT      (0x08U)     /* member is talking (PTT held) */

typedef struct
{
    uint8_t  id;
    uint8_t  flags;
    uint8_t  battery_pct;
    int8_t   rssi_dbm;
    uint16_t bearing_cdeg;
    uint16_t distance_m;
    int16_t  dalt_m;
    uint16_t age_s;
    char     name[GL_NAME_LEN + 1U];        /* NUL terminated after decode */
} gl_member_t;

typedef struct
{
    uint8_t     count;
    gl_member_t m[GL_TEAM_MAX];
} gl_team_t;

/*******************************************************************************
* Message (Tracker or phone -> goggle)
*  0 ver | 1 type | 2 from id | 3 priority | 4 time to live s (u16, 0 = keep)
*  6.. UTF-8 text, 0..GL_MSG_TEXT_MAX bytes, not terminated
*******************************************************************************/
#define GL_MSG_HDR_LEN          (6U)
#define GL_MSG_TEXT_MAX         (96U)
#define GL_MSG_MAX_LEN          (GL_MSG_HDR_LEN + GL_MSG_TEXT_MAX)

typedef enum
{
    GL_MSG_TEXT = 0,        /* team text / AI text intercom              */
    GL_MSG_SOS  = 1,        /* SOS from a member (text = name/location)  */
    GL_MSG_NAV  = 2,        /* navigation cue from the phone             */
    GL_MSG_AI   = 3,        /* AI answer (STT -> LLM -> text)            */
    GL_MSG_TYPE_COUNT
} gl_msg_type_t;

typedef struct
{
    uint8_t  type;
    uint8_t  from_id;
    uint8_t  priority;
    uint16_t ttl_s;
    uint8_t  len;
    char     text[GL_MSG_TEXT_MAX + 1U];    /* NUL terminated after decode */
} gl_msg_t;

/*******************************************************************************
* Goggle status (goggle -> Tracker / phone, read + notify), GL_STATUS_LEN bytes
*  0 ver | 1 battery % (0xFF unknown) | 2 flags | 3 link mask
*  4 speed cm/s (u16) | 6 heading 0.01 deg (u16, 0xFFFF unknown)
*  8 altitude dm (i32) | 12 temperature 0.1 C (i16) | 14 reserved (u16)
*******************************************************************************/
#define GL_STATUS_LEN           (16U)
#define GL_STATUS_FLAG_CHARGING (0x01U)
#define GL_STATUS_FLAG_HEATER   (0x02U)
#define GL_STATUS_FLAG_WIFI     (0x04U)
#define GL_STATUS_FLAG_SOS      (0x08U)

typedef struct
{
    uint8_t  battery_pct;
    uint8_t  flags;
    uint8_t  link_mask;
    uint16_t speed_cms;
    uint16_t heading_cdeg;
    int32_t  alt_dm;
    int16_t  temp_c10;
} gl_status_t;

/*******************************************************************************
* Control (Tracker or phone -> goggle)
*  0 ver | 1 command | 2.. payload
*******************************************************************************/
#define GL_CTRL_MAX_LEN         (8U)

typedef enum
{
    GL_CMD_HELLO      = 0x01,   /* u8 role (gl_role_t)                     */
    GL_CMD_SET_TIME   = 0x02,   /* u32 UTC Unix s, i8 time zone in 15 min  */
    GL_CMD_BRIGHTNESS = 0x03,   /* u8 0..100 %                             */
    GL_CMD_WIFI       = 0x04,   /* u8 0 = off, 1 = connect                 */
    GL_CMD_CLEAR_MSG  = 0x05    /* no payload                              */
} gl_cmd_t;

typedef enum
{
    GL_ROLE_UNKNOWN = 0,
    GL_ROLE_TRACKER = 1,
    GL_ROLE_PHONE   = 2
} gl_role_t;

typedef struct
{
    uint8_t cmd;
    uint8_t role;
    uint32_t utc_s;
    int8_t  tz_q15;
    uint8_t brightness;
    uint8_t wifi_on;
} gl_ctrl_t;

/*******************************************************************************
* Wi-Fi credentials (phone -> goggle, encrypted link required)
*  0 ver | 1 SSID length (1..32) | SSID | password length (0..63) | password
*******************************************************************************/
#define GL_WIFI_SSID_MAX        (32U)
#define GL_WIFI_PASS_MAX        (63U)
#define GL_WIFI_CRED_MAX_LEN    (3U + GL_WIFI_SSID_MAX + GL_WIFI_PASS_MAX)

typedef struct
{
    uint8_t ssid_len;
    uint8_t pass_len;
    char    ssid[GL_WIFI_SSID_MAX + 1U];
    char    pass[GL_WIFI_PASS_MAX + 1U];
} gl_wifi_cred_t;

/*******************************************************************************
* Result codes
*******************************************************************************/
typedef enum
{
    GL_OK = 0,
    GL_ERR_LEN,         /* wrong length                     */
    GL_ERR_VERSION,     /* unknown protocol version         */
    GL_ERR_RANGE        /* field out of range               */
} gl_result_t;

/*******************************************************************************
* Decoders (goggle side). Output is only written when GL_OK is returned.
*******************************************************************************/
gl_result_t gl_gnss_decode(const uint8_t* p, size_t len, gl_gnss_t* out);
gl_result_t gl_team_decode(const uint8_t* p, size_t len, gl_team_t* out);
gl_result_t gl_msg_decode(const uint8_t* p, size_t len, gl_msg_t* out);
gl_result_t gl_ctrl_decode(const uint8_t* p, size_t len, gl_ctrl_t* out);
gl_result_t gl_wifi_cred_decode(const uint8_t* p, size_t len, gl_wifi_cred_t* out);
gl_result_t gl_status_decode(const uint8_t* p, size_t len, gl_status_t* out);

/*******************************************************************************
* Encoders (return the number of bytes written, 0 if cap is too small).
* gl_status_encode is used by the goggle; the others by the Tracker / phone
* firmware and by the host test.
*******************************************************************************/
size_t gl_status_encode(const gl_status_t* in, uint8_t* p, size_t cap);
size_t gl_gnss_encode(const gl_gnss_t* in, uint8_t* p, size_t cap);
size_t gl_team_encode(const gl_team_t* in, uint8_t* p, size_t cap);
size_t gl_msg_encode(const gl_msg_t* in, uint8_t* p, size_t cap);

/*******************************************************************************
* HUD helpers
*******************************************************************************/
/* 8-point compass label ("N", "NE", ...) of a bearing in 0.01 deg */
const char* gl_compass8(uint16_t bearing_cdeg);

/* Relative bearing (target - heading) in 0.01 deg, wrapped to 0..35999 */
uint16_t gl_relative_bearing(uint16_t bearing_cdeg, uint16_t heading_cdeg);

/* "320M", "1.2KM", "12KM", ">65KM" into buf (cap >= 7) */
void gl_format_distance(char* buf, size_t cap, uint16_t distance_m);

/* Copies UTF-8 text to an upper-case ASCII string for the 5x7 font: a-z are
 * upper-cased, every other byte outside 0x20..0x5F (including each UTF-8
 * multi-byte sequence) becomes one '?'. Returns the output length. */
size_t gl_text_to_hud(const char* in, size_t in_len, char* out, size_t cap);

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_LINK_PROTO_H */
