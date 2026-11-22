/*******************************************************************************
* File Name        : goggle_gatt_db.c
*
* Description      : See goggle_gatt_db.h.
*
*                    Security: with GOGGLE_LINK_SECURE=1 (default) every HUD
*                    Link write needs an encrypted link, so a stranger in the
*                    lift queue cannot inject fake team positions or SOS
*                    messages. The goggle shows the 6-digit passkey on the HUD
*                    (IO capability DISPLAY_ONLY); the Tracker is meant to be
*                    bonded in the factory. Wi-Fi credentials always need
*                    encryption. Set DEFINES+=GOGGLE_LINK_SECURE=0 only for
*                    bring-up with a generic BLE app.
*
*******************************************************************************/
#include "wiced_bt_gatt.h"
#include "wiced_bt_uuid.h"
#include "gattdefs.h"

#include "goggle_gatt_db.h"
#include "goggle_link_proto.h"

#if !defined(GOGGLE_LINK_SECURE)
#define GOGGLE_LINK_SECURE      (1)
#endif

#if (GOGGLE_LINK_SECURE)
#define LINK_PERM_WRITE         (GATTDB_PERM_WRITE_REQ | GATTDB_PERM_AUTH_WRITABLE)
#define LINK_PERM_WRITE_CMD     (GATTDB_PERM_WRITE_REQ | GATTDB_PERM_WRITE_CMD | GATTDB_PERM_AUTH_WRITABLE)
#define LINK_PERM_READ          (GATTDB_PERM_READABLE | GATTDB_PERM_AUTH_READABLE)
#else
#define LINK_PERM_WRITE         (GATTDB_PERM_WRITE_REQ)
#define LINK_PERM_WRITE_CMD     (GATTDB_PERM_WRITE_REQ | GATTDB_PERM_WRITE_CMD)
#define LINK_PERM_READ          (GATTDB_PERM_READABLE)
#endif

#define CCCD_PERM               (GATTDB_PERM_READABLE | GATTDB_PERM_WRITE_REQ)

const char goggle_device_name[]  = "ASUS HUD";
const char goggle_dis_mfr_name[] = "ASUS";
const char goggle_dis_model[]    = "HUD-GOGGLE-POC";
const char goggle_dis_fw_rev[]   = "0.1.0";

const uint8_t goggle_gatt_db[] =
{
    /* ---------------- Generic Access ---------------- */
    PRIMARY_SERVICE_UUID16(HDLS_GAP, UUID_SERVICE_GAP),
        CHARACTERISTIC_UUID16(HDLC_GAP_DEVICE_NAME, HDLC_GAP_DEVICE_NAME_VALUE,
                              UUID_CHARACTERISTIC_DEVICE_NAME,
                              GATTDB_CHAR_PROP_READ, GATTDB_PERM_READABLE),
        CHARACTERISTIC_UUID16(HDLC_GAP_APPEARANCE, HDLC_GAP_APPEARANCE_VALUE,
                              UUID_CHARACTERISTIC_APPEARANCE,
                              GATTDB_CHAR_PROP_READ, GATTDB_PERM_READABLE),

    /* ---------------- Generic Attribute ---------------- */
    PRIMARY_SERVICE_UUID16(HDLS_GATT, UUID_SERVICE_GATT),

    /* ---------------- Battery ---------------- */
    PRIMARY_SERVICE_UUID16(HDLS_BAS, UUID_SERVICE_BATTERY),
        CHARACTERISTIC_UUID16(HDLC_BAS_LEVEL, HDLC_BAS_LEVEL_VALUE,
                              UUID_CHARACTERISTIC_BATTERY_LEVEL,
                              GATTDB_CHAR_PROP_READ | GATTDB_CHAR_PROP_NOTIFY,
                              GATTDB_PERM_READABLE),
            CHAR_DESCRIPTOR_UUID16_WRITABLE(HDLD_BAS_LEVEL_CCCD,
                                            UUID_DESCRIPTOR_CLIENT_CHARACTERISTIC_CONFIGURATION,
                                            CCCD_PERM),

    /* ---------------- Device Information ---------------- */
    PRIMARY_SERVICE_UUID16(HDLS_DIS, UUID_SERVICE_DEVICE_INFORMATION),
        CHARACTERISTIC_UUID16(HDLC_DIS_MFR_NAME, HDLC_DIS_MFR_NAME_VALUE,
                              UUID_CHARACTERISTIC_MANUFACTURER_NAME_STRING,
                              GATTDB_CHAR_PROP_READ, GATTDB_PERM_READABLE),
        CHARACTERISTIC_UUID16(HDLC_DIS_MODEL, HDLC_DIS_MODEL_VALUE,
                              UUID_CHARACTERISTIC_MODEL_NUMBER_STRING,
                              GATTDB_CHAR_PROP_READ, GATTDB_PERM_READABLE),
        CHARACTERISTIC_UUID16(HDLC_DIS_FW_REV, HDLC_DIS_FW_REV_VALUE,
                              UUID_CHARACTERISTIC_FIRMWARE_REVISION_STRING,
                              GATTDB_CHAR_PROP_READ, GATTDB_PERM_READABLE),

    /* ---------------- HUD Link (custom) ---------------- */
    PRIMARY_SERVICE_UUID128(HDLS_LINK, GL_UUID_SERVICE),

        /* Tracker GNSS fix: write without response at 1 Hz */
        CHARACTERISTIC_UUID128_WRITABLE(HDLC_LINK_GNSS, HDLC_LINK_GNSS_VALUE,
                                        GL_UUID_TRACKER_GNSS,
                                        GATTDB_CHAR_PROP_WRITE | GATTDB_CHAR_PROP_WRITE_NO_RESPONSE,
                                        LINK_PERM_WRITE_CMD | GATTDB_PERM_VARIABLE_LENGTH),

        /* Team members relative to the Tracker */
        CHARACTERISTIC_UUID128_WRITABLE(HDLC_LINK_TEAM, HDLC_LINK_TEAM_VALUE,
                                        GL_UUID_TEAM,
                                        GATTDB_CHAR_PROP_WRITE | GATTDB_CHAR_PROP_WRITE_NO_RESPONSE,
                                        LINK_PERM_WRITE_CMD | GATTDB_PERM_VARIABLE_LENGTH),

        /* Text / SOS / navigation / AI message: acknowledged write */
        CHARACTERISTIC_UUID128_WRITABLE(HDLC_LINK_MESSAGE, HDLC_LINK_MESSAGE_VALUE,
                                        GL_UUID_MESSAGE,
                                        GATTDB_CHAR_PROP_WRITE,
                                        LINK_PERM_WRITE | GATTDB_PERM_VARIABLE_LENGTH),

        /* Goggle status: read + notify (1 Hz while subscribed) */
        CHARACTERISTIC_UUID128(HDLC_LINK_STATUS, HDLC_LINK_STATUS_VALUE,
                               GL_UUID_GOGGLE_STATUS,
                               GATTDB_CHAR_PROP_READ | GATTDB_CHAR_PROP_NOTIFY,
                               LINK_PERM_READ),
            CHAR_DESCRIPTOR_UUID16_WRITABLE(HDLD_LINK_STATUS_CCCD,
                                            UUID_DESCRIPTOR_CLIENT_CHARACTERISTIC_CONFIGURATION,
                                            CCCD_PERM),

        /* Commands (HELLO role, time, brightness, Wi-Fi on/off, clear) */
        CHARACTERISTIC_UUID128_WRITABLE(HDLC_LINK_CONTROL, HDLC_LINK_CONTROL_VALUE,
                                        GL_UUID_CONTROL,
                                        GATTDB_CHAR_PROP_WRITE,
                                        LINK_PERM_WRITE | GATTDB_PERM_VARIABLE_LENGTH),

        /* Wi-Fi credentials: always encrypted */
        CHARACTERISTIC_UUID128_WRITABLE(HDLC_LINK_WIFI_CRED, HDLC_LINK_WIFI_CRED_VALUE,
                                        GL_UUID_WIFI_CRED,
                                        GATTDB_CHAR_PROP_WRITE,
                                        GATTDB_PERM_WRITE_REQ | GATTDB_PERM_AUTH_WRITABLE |
                                        GATTDB_PERM_VARIABLE_LENGTH),
};

const uint16_t goggle_gatt_db_len = (uint16_t)sizeof(goggle_gatt_db);
