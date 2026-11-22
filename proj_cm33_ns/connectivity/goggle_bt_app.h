/*******************************************************************************
* File Name        : goggle_bt_app.h
*
* Description      : Bluetooth LE application of the goggle (CM33 non-secure).
*                    The goggle is a GATT server / LE peripheral with up to
*                    two links at the same time: the LoRa Tracker (sends its
*                    GNSS fix, the team positions it receives over LoRa and
*                    team messages) and the phone (navigation cues, AI text,
*                    time, Wi-Fi provisioning). Received data is published to
*                    the CM55 HUD through goggle_link_shm.
*
*******************************************************************************/
#ifndef GOGGLE_BT_APP_H
#define GOGGLE_BT_APP_H

#include "cy_result.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Initialises NVM, the shared mailbox and the BT stack, and creates the
 * 1 Hz link task. Call from main() before vTaskStartScheduler() and before
 * the CM55 is enabled. */
cy_rslt_t goggle_bt_app_start(void);

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_BT_APP_H */
