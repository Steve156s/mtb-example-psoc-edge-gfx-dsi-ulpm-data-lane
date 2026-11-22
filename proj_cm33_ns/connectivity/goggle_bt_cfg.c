/*******************************************************************************
* File Name        : goggle_bt_cfg.c
*
* Description      : AIROC BT stack configuration of the goggle (LE only,
*                    peripheral, two simultaneous links: Tracker + phone).
*                    Replaces the cycfg_bt_settings.c that the Bluetooth
*                    Configurator would generate. Designated initialisers are
*                    used so that fields added by newer btstack releases
*                    default to zero.
*
*                    Classic BR/EDR (A2DP / HFP to a SHOKZ headset) is not
*                    used by the goggle: in the product the Tracker owns the
*                    headset. To add it, set p_br_cfg and COMPONENTS+=
*                    WICED_DUALMODE (see docs/bt_wifi_connectivity.md).
*
*******************************************************************************/
#include "wiced_bt_cfg.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_ble.h"
#include "gattdefs.h"

#include "goggle_bt_cfg.h"
#include "goggle_gatt_db.h"
#include "goggle_link_proto.h"

static const wiced_bt_cfg_ble_scan_settings_t ble_scan_cfg =
{
    /* The goggle never scans; values are the stack defaults */
    .scan_mode                    = BTM_BLE_SCAN_MODE_PASSIVE,
    .high_duty_scan_interval      = WICED_BT_CFG_DEFAULT_HIGH_DUTY_SCAN_INTERVAL,
    .high_duty_scan_window        = WICED_BT_CFG_DEFAULT_HIGH_DUTY_SCAN_WINDOW,
    .high_duty_scan_duration      = 5U,
    .low_duty_scan_interval       = WICED_BT_CFG_DEFAULT_LOW_DUTY_SCAN_INTERVAL,
    .low_duty_scan_window         = WICED_BT_CFG_DEFAULT_LOW_DUTY_SCAN_WINDOW,
    .low_duty_scan_duration       = 5U,
    .high_duty_conn_scan_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_CONN_SCAN_INTERVAL,
    .high_duty_conn_scan_window   = WICED_BT_CFG_DEFAULT_HIGH_DUTY_CONN_SCAN_WINDOW,
    .high_duty_conn_duration      = 30U,
    .low_duty_conn_scan_interval  = WICED_BT_CFG_DEFAULT_LOW_DUTY_CONN_SCAN_INTERVAL,
    .low_duty_conn_scan_window    = WICED_BT_CFG_DEFAULT_LOW_DUTY_CONN_SCAN_WINDOW,
    .low_duty_conn_duration       = 30U,
    /* Preferred connection: 30..50 ms, latency 4, 6 s supervision. The
     * Tracker sends 1 Hz GNSS + team updates, so this keeps the radio duty
     * cycle low while a message still arrives within ~250 ms. */
    .conn_min_interval            = 24U,
    .conn_max_interval            = 40U,
    .conn_latency                 = 4U,
    .conn_supervision_timeout     = 600U
};

static const wiced_bt_cfg_ble_advert_settings_t ble_advert_cfg =
{
    .channel_map                     = BTM_BLE_ADVERT_CHNL_37 | BTM_BLE_ADVERT_CHNL_38 |
                                       BTM_BLE_ADVERT_CHNL_39,
    /* fast advertising for 30 s after boot / disconnect: 30 ms */
    .high_duty_min_interval          = WICED_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MIN_INTERVAL,
    .high_duty_max_interval          = WICED_BT_CFG_DEFAULT_HIGH_DUTY_ADV_MAX_INTERVAL,
    .high_duty_duration              = 30U,
    /* then slow advertising forever: 1.28 s (low power, reconnect < 2 s) */
    .low_duty_min_interval           = WICED_BT_CFG_DEFAULT_LOW_DUTY_ADV_MIN_INTERVAL,
    .low_duty_max_interval           = WICED_BT_CFG_DEFAULT_LOW_DUTY_ADV_MAX_INTERVAL,
    .low_duty_duration               = 0U,
    .high_duty_directed_min_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_DIRECTED_ADV_MIN_INTERVAL,
    .high_duty_directed_max_interval = WICED_BT_CFG_DEFAULT_HIGH_DUTY_DIRECTED_ADV_MAX_INTERVAL,
    .low_duty_directed_min_interval  = WICED_BT_CFG_DEFAULT_LOW_DUTY_DIRECTED_ADV_MIN_INTERVAL,
    .low_duty_directed_max_interval  = WICED_BT_CFG_DEFAULT_LOW_DUTY_DIRECTED_ADV_MAX_INTERVAL,
    .low_duty_directed_duration      = 30U,
    .high_duty_nonconn_min_interval  = WICED_BT_CFG_DEFAULT_HIGH_DUTY_NONCONN_ADV_MIN_INTERVAL,
    .high_duty_nonconn_max_interval  = WICED_BT_CFG_DEFAULT_HIGH_DUTY_NONCONN_ADV_MAX_INTERVAL,
    .high_duty_nonconn_duration      = 30U,
    .low_duty_nonconn_min_interval   = WICED_BT_CFG_DEFAULT_LOW_DUTY_NONCONN_ADV_MIN_INTERVAL,
    .low_duty_nonconn_max_interval   = WICED_BT_CFG_DEFAULT_LOW_DUTY_NONCONN_ADV_MAX_INTERVAL,
    .low_duty_nonconn_duration       = 0U
};

static const wiced_bt_cfg_ble_t ble_cfg =
{
    .ble_max_simultaneous_links   = GOGGLE_BT_MAX_LINKS,
    .ble_max_rx_pdu_size          = GOGGLE_BT_MAX_MTU + 4U,  /* + L2CAP header */
    .appearance                   = APPEARANCE_GENERIC_EYE_GLASSES,
    .rpa_refresh_timeout          = WICED_BT_CFG_DEFAULT_RANDOM_ADDRESS_NEVER_CHANGE,
    .host_addr_resolution_db_size = 5U,
    .p_ble_scan_cfg               = &ble_scan_cfg,
    .p_ble_advert_cfg             = &ble_advert_cfg
};

static const wiced_bt_cfg_gatt_t gatt_cfg =
{
    .max_db_service_modules = 0U,
    .max_eatt_bearers       = 0U
};

const wiced_bt_cfg_settings_t goggle_bt_cfg_settings =
{
    .device_name       = (uint8_t*)goggle_device_name,
    .security_required = BTM_SEC_BEST_EFFORT,
    .p_br_cfg          = NULL,          /* LE only */
    .p_ble_cfg         = &ble_cfg,
    .p_gatt_cfg        = &gatt_cfg,
    .p_isoc_cfg        = NULL,
    .p_l2cap_app_cfg   = NULL
};

/*******************************************************************************
* Advertising data: flags, 128-bit HUD Link service UUID (so the Tracker and
* the phone app can filter), complete name. 3 + 18 + 10 = 31 bytes.
*******************************************************************************/
static uint8_t adv_flags = BTM_BLE_GENERAL_DISCOVERABLE_FLAG | BTM_BLE_BREDR_NOT_SUPPORTED;
static uint8_t adv_uuid[16] = { GL_UUID_SERVICE };

static wiced_bt_ble_advert_elem_t adv_elems[] =
{
    { .p_data = &adv_flags, .len = 1U,  .advert_type = BTM_BLE_ADVERT_TYPE_FLAG },
    { .p_data = adv_uuid,   .len = 16U, .advert_type = BTM_BLE_ADVERT_TYPE_128SRV_COMPLETE },
    { .p_data = (uint8_t*)goggle_device_name,
      .len = (uint16_t)(sizeof("ASUS HUD") - 1U),
      .advert_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE },
};

wiced_bt_ble_advert_elem_t* goggle_bt_adv_elems(uint8_t* count)
{
    *count = (uint8_t)(sizeof(adv_elems) / sizeof(adv_elems[0]));
    return adv_elems;
}
