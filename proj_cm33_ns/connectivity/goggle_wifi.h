/*******************************************************************************
* File Name        : goggle_wifi.h
*
* Description      : Optional Wi-Fi station on the CYW55513 (KIT_PSE84_AI /
*                    KIT_PSE84_EVAL) through the Wi-Fi Connection Manager.
*                    Off by default: the radio is powered (WL_REG_ON) only
*                    while the phone asks for it (GL_CMD_WIFI = 1) or right
*                    after new credentials are provisioned over BLE. Intended
*                    for OTA updates, log / track upload and map download in
*                    the lodge, not for use on the slope.
*
*******************************************************************************/
#ifndef GOGGLE_WIFI_H
#define GOGGLE_WIFI_H

#include "cy_result.h"
#include "goggle_link_shm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Seconds after which a connected Wi-Fi is switched off again (0 = never) */
#if !defined(GOGGLE_WIFI_AUTO_OFF_S)
#define GOGGLE_WIFI_AUTO_OFF_S      (600U)
#endif

/* Creates the Wi-Fi task (radio stays off). */
cy_rslt_t goggle_wifi_start(void);

/* Asynchronous requests (safe from the BT stack callbacks) */
void goggle_wifi_connect_saved(void);   /* uses the credentials in NVM */
void goggle_wifi_disconnect(void);

gl_wifi_state_t goggle_wifi_state(void);

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_WIFI_H */
