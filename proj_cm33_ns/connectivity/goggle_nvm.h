/*******************************************************************************
* File Name        : goggle_nvm.h
*
* Description      : Persistent storage of the Bluetooth bonds (so the phone
*                    and the factory-paired Tracker reconnect after a reboot
*                    without "forget device") and of the Wi-Fi credentials.
*                    One record in the RRAM "user_nvm" region of the CM33
*                    (CYMEM_CM33_0_user_nvm_START), the same region the
*                    Infineon Wi-Fi onboarding example uses.
*
*******************************************************************************/
#ifndef GOGGLE_NVM_H
#define GOGGLE_NVM_H

#include <stdbool.h>
#include <stdint.h>

#include "wiced_bt_dev.h"
#include "goggle_link_proto.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define GOGGLE_NVM_MAX_BONDS    (4U)    /* Tracker + phone + spares */

/* Loads the record (or starts empty if missing / corrupt). */
void goggle_nvm_init(void);

/* Bonds */
bool goggle_nvm_find_bond(const wiced_bt_device_address_t bd_addr,
                          wiced_bt_device_link_keys_t* out);
void goggle_nvm_save_bond(const wiced_bt_device_link_keys_t* keys);
void goggle_nvm_clear_bonds(void);
uint32_t goggle_nvm_bond_count(void);
const wiced_bt_device_link_keys_t* goggle_nvm_bond_at(uint32_t index);

/* Local identity keys (IRK / ER) */
bool goggle_nvm_get_local_keys(wiced_bt_local_identity_keys_t* out);
void goggle_nvm_save_local_keys(const wiced_bt_local_identity_keys_t* keys);

/* Wi-Fi credentials */
bool goggle_nvm_get_wifi(gl_wifi_cred_t* out);
void goggle_nvm_save_wifi(const gl_wifi_cred_t* cred);

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_NVM_H */
