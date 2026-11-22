/*******************************************************************************
* File Name        : goggle_bt_cfg.h
*
* Description      : BT stack configuration and advertising data.
*
*******************************************************************************/
#ifndef GOGGLE_BT_CFG_H
#define GOGGLE_BT_CFG_H

#include "wiced_bt_cfg.h"
#include "wiced_bt_ble.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define GOGGLE_BT_MAX_LINKS     (2U)    /* Tracker + phone           */
#define GOGGLE_BT_MAX_MTU       (247U)  /* team packet needs >= 133  */

extern const wiced_bt_cfg_settings_t goggle_bt_cfg_settings;

wiced_bt_ble_advert_elem_t* goggle_bt_adv_elems(uint8_t* count);

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_BT_CFG_H */
