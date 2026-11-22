/*******************************************************************************
* File Name        : goggle_gatt_db.h
*
* Description      : Hand-written GATT database of the goggle (GAP, GATT,
*                    Battery, Device Information and the custom HUD Link
*                    service). Written in C instead of a Bluetooth
*                    Configurator design.cybt so that the service definition
*                    can be reviewed in one place and kept in sync with
*                    shared/goggle_link/goggle_link_proto.h.
*
*******************************************************************************/
#ifndef GOGGLE_GATT_DB_H
#define GOGGLE_GATT_DB_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Attribute handles (must be ascending in the database) */
enum
{
    HDLS_GAP                    = 0x0001,
    HDLC_GAP_DEVICE_NAME        = 0x0002,
    HDLC_GAP_DEVICE_NAME_VALUE  = 0x0003,
    HDLC_GAP_APPEARANCE         = 0x0004,
    HDLC_GAP_APPEARANCE_VALUE   = 0x0005,

    HDLS_GATT                   = 0x0006,

    HDLS_BAS                    = 0x0010,
    HDLC_BAS_LEVEL              = 0x0011,
    HDLC_BAS_LEVEL_VALUE        = 0x0012,
    HDLD_BAS_LEVEL_CCCD         = 0x0013,

    HDLS_DIS                    = 0x0018,
    HDLC_DIS_MFR_NAME           = 0x0019,
    HDLC_DIS_MFR_NAME_VALUE     = 0x001A,
    HDLC_DIS_MODEL              = 0x001B,
    HDLC_DIS_MODEL_VALUE        = 0x001C,
    HDLC_DIS_FW_REV             = 0x001D,
    HDLC_DIS_FW_REV_VALUE       = 0x001E,

    HDLS_LINK                   = 0x0020,
    HDLC_LINK_GNSS              = 0x0021,
    HDLC_LINK_GNSS_VALUE        = 0x0022,
    HDLC_LINK_TEAM              = 0x0023,
    HDLC_LINK_TEAM_VALUE        = 0x0024,
    HDLC_LINK_MESSAGE           = 0x0025,
    HDLC_LINK_MESSAGE_VALUE     = 0x0026,
    HDLC_LINK_STATUS            = 0x0027,
    HDLC_LINK_STATUS_VALUE      = 0x0028,
    HDLD_LINK_STATUS_CCCD       = 0x0029,
    HDLC_LINK_CONTROL           = 0x002A,
    HDLC_LINK_CONTROL_VALUE     = 0x002B,
    HDLC_LINK_WIFI_CRED         = 0x002C,
    HDLC_LINK_WIFI_CRED_VALUE   = 0x002D
};

extern const uint8_t  goggle_gatt_db[];
extern const uint16_t goggle_gatt_db_len;

/* Strings / values served by the read handler */
extern const char     goggle_device_name[];
extern const char     goggle_dis_mfr_name[];
extern const char     goggle_dis_model[];
extern const char     goggle_dis_fw_rev[];

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_GATT_DB_H */
