/*******************************************************************************
* File Name        : goggle_nvm.c
*
* Description      : See goggle_nvm.h. The record is kept in RAM and written
*                    back as a whole (bonding and Wi-Fi provisioning are rare
*                    events, so RRAM wear is not a concern).
*
*******************************************************************************/
#include <stddef.h>
#include <string.h>

#include "cybsp.h"
#include "cy_rram.h"

#include "goggle_nvm.h"
#include "goggle_log.h"

#define NVM_MAGIC           (0x47564E4DUL)     /* 'GNVM' */
#define NVM_VERSION         (1U)
#define NVM_ADDR            (CYMEM_CM33_0_user_nvm_START)

typedef struct
{
    uint32_t                        magic;
    uint16_t                        version;
    uint16_t                        size;
    uint8_t                         local_keys_valid;
    uint8_t                         wifi_valid;
    uint8_t                         bond_count;
    uint8_t                         bond_next;      /* round-robin slot */
    wiced_bt_local_identity_keys_t  local_keys;
    gl_wifi_cred_t                  wifi;
    wiced_bt_device_link_keys_t     bonds[GOGGLE_NVM_MAX_BONDS];
    uint32_t                        crc;
} nvm_record_t;

/* The record must fit in the user_nvm region of the BSP memory map */
typedef char nvm_fits_user_nvm[(sizeof(nvm_record_t) <= CYMEM_CM33_0_user_nvm_SIZE) ? 1 : -1];

static nvm_record_t rec;

/*******************************************************************************
* CRC-32 (IEEE 802.3), bitwise: the record is small and written rarely
*******************************************************************************/
static uint32_t crc32_calc(const uint8_t* p, size_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for (size_t i = 0U; i < len; i++)
    {
        crc ^= p[i];
        for (uint32_t b = 0U; b < 8U; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return ~crc;
}

static uint32_t record_crc(const nvm_record_t* r)
{
    return crc32_calc((const uint8_t*)r, offsetof(nvm_record_t, crc));
}

static void record_reset(void)
{
    memset(&rec, 0, sizeof(rec));
    rec.magic   = NVM_MAGIC;
    rec.version = NVM_VERSION;
    rec.size    = (uint16_t)sizeof(rec);
}

static void record_flush(void)
{
    cy_en_rram_status_t st;

    rec.crc = record_crc(&rec);
    st = Cy_RRAM_WriteByteArray(RRAMC0, NVM_ADDR, (uint8_t*)&rec, sizeof(rec));
    if (CY_RRAM_SUCCESS != st)
    {
        GL_LOG("NVM: write failed 0x%lx", (unsigned long)st);
    }
}

/*******************************************************************************
* API
*******************************************************************************/
void goggle_nvm_init(void)
{
    cy_en_rram_status_t st;

    st = Cy_RRAM_NvmReadByteArray(RRAMC0, NVM_ADDR, (uint8_t*)&rec, sizeof(rec));

    if ((CY_RRAM_SUCCESS != st) || (NVM_MAGIC != rec.magic) ||
        (NVM_VERSION != rec.version) || (sizeof(rec) != rec.size) ||
        (record_crc(&rec) != rec.crc) || (rec.bond_count > GOGGLE_NVM_MAX_BONDS))
    {
        GL_LOG("NVM: no valid record, starting empty");
        record_reset();
        return;
    }

    GL_LOG("NVM: %u bond(s), Wi-Fi %s", (unsigned)rec.bond_count,
           rec.wifi_valid ? "provisioned" : "not provisioned");
}

bool goggle_nvm_find_bond(const wiced_bt_device_address_t bd_addr,
                          wiced_bt_device_link_keys_t* out)
{
    for (uint32_t i = 0U; i < rec.bond_count; i++)
    {
        if (0 == memcmp(rec.bonds[i].bd_addr, bd_addr, sizeof(wiced_bt_device_address_t)))
        {
            *out = rec.bonds[i];
            return true;
        }
    }
    return false;
}

void goggle_nvm_save_bond(const wiced_bt_device_link_keys_t* keys)
{
    uint32_t slot = rec.bond_count;

    for (uint32_t i = 0U; i < rec.bond_count; i++)
    {
        if (0 == memcmp(rec.bonds[i].bd_addr, keys->bd_addr, sizeof(wiced_bt_device_address_t)))
        {
            slot = i;   /* update in place */
            break;
        }
    }

    if (slot >= GOGGLE_NVM_MAX_BONDS)
    {
        /* table full: overwrite the oldest slot */
        slot = rec.bond_next % GOGGLE_NVM_MAX_BONDS;
        rec.bond_next = (uint8_t)((slot + 1U) % GOGGLE_NVM_MAX_BONDS);
    }
    else if (slot == rec.bond_count)
    {
        rec.bond_count++;
    }

    rec.bonds[slot] = *keys;
    record_flush();
}

void goggle_nvm_clear_bonds(void)
{
    rec.bond_count = 0U;
    rec.bond_next  = 0U;
    memset(rec.bonds, 0, sizeof(rec.bonds));
    record_flush();
}

uint32_t goggle_nvm_bond_count(void)
{
    return rec.bond_count;
}

const wiced_bt_device_link_keys_t* goggle_nvm_bond_at(uint32_t index)
{
    return (index < rec.bond_count) ? &rec.bonds[index] : NULL;
}

bool goggle_nvm_get_local_keys(wiced_bt_local_identity_keys_t* out)
{
    if (0U == rec.local_keys_valid)
    {
        return false;
    }
    *out = rec.local_keys;
    return true;
}

void goggle_nvm_save_local_keys(const wiced_bt_local_identity_keys_t* keys)
{
    rec.local_keys = *keys;
    rec.local_keys_valid = 1U;
    record_flush();
}

bool goggle_nvm_get_wifi(gl_wifi_cred_t* out)
{
    if (0U == rec.wifi_valid)
    {
        return false;
    }
    *out = rec.wifi;
    return true;
}

void goggle_nvm_save_wifi(const gl_wifi_cred_t* cred)
{
    rec.wifi = *cred;
    rec.wifi_valid = 1U;
    record_flush();
}
