/*******************************************************************************
* File Name        : goggle_link_shm.h
*
* Description      : Mailbox between the CM33 non-secure core (Bluetooth LE
*                    and Wi-Fi, owner of the CYW55513 / CYW55310) and the CM55
*                    core (graphics / HUD).
*
*                    It lives at the end of the BSP region "m33_m55_shared"
*                    (SoCMEM, 256 KB, shared by both non-secure cores). Three
*                    blocks, each on its own 32-byte cache lines:
*                      down : CM33 -> CM55, link state + Tracker / team data
*                      up   : CM55 -> CM33, goggle status for the phone
*                      log  : CM33 -> CM55, text log ring (the CM55 owns the
*                             debug UART, the CM33 must not print)
*
*                    Each data block is a seqlock: the writer makes the
*                    sequence odd, copies, makes it even again; the reader
*                    retries while it is odd or changed. No IPC interrupt is
*                    needed because the HUD polls at its frame / refresh rate.
*
*******************************************************************************/
#ifndef GOGGLE_LINK_SHM_H
#define GOGGLE_LINK_SHM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "goggle_link_proto.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define GL_SHM_MAGIC            (0x474C4E4BUL)     /* 'GLNK' */
#define GL_SHM_LINE             (32U)              /* cache line size  */
#define GL_LOG_RING_SIZE        (2048U)            /* power of two     */

/* Size reserved at the end of m33_m55_shared (must hold gl_shm_t) */
#define GL_SHM_RESERVED         (0x2000U)

/* link_mask bits */
#define GL_LINK_TRACKER         (0x01U)     /* Tracker connected        */
#define GL_LINK_PHONE           (0x02U)     /* phone connected          */
#define GL_LINK_WIFI            (0x04U)     /* Wi-Fi associated + IP    */
#define GL_LINK_ADV             (0x08U)     /* LE advertising           */
#define GL_LINK_BT_READY        (0x10U)     /* BT stack enabled         */
#define GL_LINK_PAIRING         (0x20U)     /* passkey shown on the HUD */

typedef enum
{
    GL_WIFI_OFF = 0,
    GL_WIFI_CONNECTING,
    GL_WIFI_CONNECTED,
    GL_WIFI_FAILED
} gl_wifi_state_t;

#define GL_ALIGNED      __attribute__((aligned(GL_SHM_LINE)))

/* CM33 -> CM55 */
typedef struct
{
    volatile uint32_t seq;
    uint32_t    magic;
    uint32_t    cm33_tick_ms;       /* CM33 time base for the *_rx_ms fields */
    uint8_t     link_mask;
    uint8_t     wifi_state;         /* gl_wifi_state_t                       */
    uint8_t     brightness;         /* 0..100, from GL_CMD_BRIGHTNESS        */
    int8_t      tz_q15;
    uint32_t    passkey;            /* valid while GL_LINK_PAIRING is set    */
    uint32_t    wifi_ipv4;          /* network byte order, 0 = none          */
    uint32_t    utc_s;              /* last GL_CMD_SET_TIME / GNSS time      */
    uint32_t    utc_rx_ms;
    bool        gnss_valid;
    uint32_t    gnss_rx_ms;
    gl_gnss_t   gnss;
    uint32_t    team_rx_ms;
    gl_team_t   team;
    bool        msg_valid;
    uint32_t    msg_rx_ms;
    gl_msg_t    msg;
} GL_ALIGNED gl_down_t;

/* CM55 -> CM33 */
typedef struct
{
    volatile uint32_t seq;
    uint32_t    magic;
    gl_status_t status;
} GL_ALIGNED gl_up_t;

/* CM33 -> CM55 log ring, single producer / single consumer */
typedef struct
{
    volatile uint32_t head;         /* written by the CM33 only */
    uint32_t    magic;
} GL_ALIGNED gl_log_head_t;

typedef struct
{
    volatile uint32_t tail;         /* written by the CM55 only */
} GL_ALIGNED gl_log_tail_t;

typedef struct
{
    gl_down_t       down;
    gl_up_t         up;
    gl_log_head_t   log_head;
    gl_log_tail_t   log_tail;
    char            log_buf[GL_LOG_RING_SIZE] GL_ALIGNED;
} GL_ALIGNED gl_shm_t;

/*******************************************************************************
* Port hooks (weak defaults in goggle_link_shm.c)
*******************************************************************************/
/* Writer-side lock on the CM33 (BT stack task and Wi-Fi task both write).
 * Default: no lock. The CM33 project overrides them with a critical section. */
void gl_shm_port_lock(void);
void gl_shm_port_unlock(void);

/*******************************************************************************
* API
*******************************************************************************/
/* Address of the mailbox (end of m33_m55_shared on the target, a static
 * buffer when GL_SHM_HOST_TEST is defined). */
gl_shm_t* gl_shm(void);

/* CM33: clears the whole mailbox and stamps the magic numbers. Call once
 * before the CM55 is started. */
void gl_shm_init_owner(void);

/* true once the CM33 has initialised the mailbox */
bool gl_shm_ready(void);

/* Writer: runs fn(block, ctx) inside the seqlock (and the port lock) */
typedef void (*gl_down_fn_t)(gl_down_t* d, void* ctx);
typedef void (*gl_up_fn_t)(gl_up_t* u, void* ctx);
void gl_shm_down_write(gl_down_fn_t fn, void* ctx);   /* CM33 */
void gl_shm_up_write(gl_up_fn_t fn, void* ctx);       /* CM55 */

/* Reader: consistent snapshot. Returns false if no stable copy was obtained
 * after a few retries or the mailbox is not initialised. */
bool gl_shm_down_read(gl_down_t* out);                  /* CM55 */
bool gl_shm_up_read(gl_up_t* out);                      /* CM33 */

/* Log ring. Push drops the line if it does not fit (never blocks).
 * Pop copies up to cap bytes and returns the count (0 = empty). */
void   gl_shm_log_push(const char* text, size_t len);   /* CM33 */
size_t gl_shm_log_pop(char* out, size_t cap);           /* CM55 */

#if defined(__cplusplus)
}
#endif

#endif /* GOGGLE_LINK_SHM_H */
