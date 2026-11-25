/*******************************************************************************
* File Name        : stt_link.c
*
* Description      : see stt_link.h
*
* Device Configurator setup (design.modus, then save to regenerate cycfg_*):
*   1. Debug UART (CYBSP_DEBUG_UART, KitProg3 bridge): raise the baud rate to
*      921600 by changing the SCB clock divider; 115200 works but a 8 s
*      recording then takes ~22 s to upload. Match --baud in stt_bridge.py.
*   2. Heltec UART: enable a free SCB as UART 115200 8N1 and give it the
*      name HELTEC_UART so the generated symbols are CYBSP_HELTEC_UART_HW /
*      CYBSP_HELTEC_UART_config. Pins: J7 P17.0/P17.1 (3.3 V, if that SCB
*      offers UART there) or J5 (1.8 V, through a TXB0104 level shifter).
*   3. PTT button: CYBSP_USER_BTN (SW1). If another task already owns the
*      button interrupt, change STT_LINK_PTT_PORT/PIN.
*
* Keep other printf() traffic low while testing: the PC UART carries binary
* frames; stray text between frames is skipped by the PC decoder, but text
* written in the middle of a frame breaks that frame's CRC.
*******************************************************************************/
#include "stt_link.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cy_pdl.h"
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "link_frame.h"

/*******************************************************************************
* Configuration
*******************************************************************************/
#define PC_UART_HW               CYBSP_DEBUG_UART_HW

#ifndef STT_LINK_HELTEC_HW
#define STT_LINK_HELTEC_HW       CYBSP_HELTEC_UART_HW
#define STT_LINK_HELTEC_CONFIG   CYBSP_HELTEC_UART_config
#endif

#ifndef STT_LINK_PTT_PORT
#define STT_LINK_PTT_PORT        CYBSP_USER_BTN_PORT
#define STT_LINK_PTT_PIN         CYBSP_USER_BTN_PIN
#endif
#define PTT_PRESSED_LEVEL        (0U)        /* buttons pull to GND */
#define PTT_MIN_MS               (300U)      /* shorter press = ignored */

#define TASK_PERIOD_MS           (5U)
#define AUDIO_CHUNK_BYTES        (1024U)
#define STT_LINK_RSLT_ERR        CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, CY_RSLT_MODULE_MIDDLEWARE_BASE, 0x5A0U)
#define MAX_SAMPLES              (STT_LINK_SAMPLE_RATE * STT_LINK_MAX_SECONDS)

/*******************************************************************************
* State
*******************************************************************************/
/* 256 KB recording buffer in SoCMEM (PD1, 5 MB) */
CY_SECTION(".cy_socmem_data") static int16_t rec_buf[MAX_SAMPLES];

static volatile size_t   rec_count;
static volatile bool     ptt_down;
static cy_stc_scb_uart_context_t heltec_ctx;
static link_decoder_t    dec_pc;
static link_decoder_t    dec_heltec;
static SemaphoreHandle_t pc_tx_lock;
static SemaphoreHandle_t heltec_tx_lock;
static uint8_t           seq_pc;
static uint8_t           seq_heltec;
static uint8_t           frame_buf[LINK_MAX_FRAME];
static uint8_t           heltec_frame_buf[LINK_HDR_LEN + LINK_MAX_TEXT + LINK_CRC_LEN];

/*******************************************************************************
* UART helpers
*******************************************************************************/
static void uart_write(CySCB_Type *hw, const uint8_t *data, size_t len)
{
    Cy_SCB_UART_PutArrayBlocking(hw, (void *)data, (uint32_t)len);
    while (!Cy_SCB_UART_IsTxComplete(hw)) { }
}

static void pc_send(uint8_t type, const uint8_t *payload, uint16_t len)
{
    xSemaphoreTake(pc_tx_lock, portMAX_DELAY);
    size_t n = link_encode(type, seq_pc++, payload, len, frame_buf, sizeof frame_buf);
    if (n > 0U) { uart_write(PC_UART_HW, frame_buf, n); }
    xSemaphoreGive(pc_tx_lock);
}

static void pc_log(const char *fmt, ...)
{
    char msg[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (n > 0) { pc_send(LINK_T_LOG, (const uint8_t *)msg, (uint16_t)((n < (int)sizeof msg) ? n : (int)sizeof msg - 1)); }
}

static void heltec_send(uint8_t type, const uint8_t *payload, uint16_t len)
{
    xSemaphoreTake(heltec_tx_lock, portMAX_DELAY);
    size_t n = link_encode(type, seq_heltec++, payload, len, heltec_frame_buf, sizeof heltec_frame_buf);
    if (n > 0U) { uart_write(STT_LINK_HELTEC_HW, heltec_frame_buf, n); }
    xSemaphoreGive(heltec_tx_lock);
}

/*******************************************************************************
* Public API
*******************************************************************************/
void stt_link_push_pcm(const int16_t *samples, size_t count)
{
    if (!ptt_down) { return; }
    size_t n = rec_count;
    size_t room = MAX_SAMPLES - n;
    if (count > room) { count = room; }
    memcpy(&rec_buf[n], samples, count * sizeof(int16_t));
    rec_count = n + count;
}

void stt_link_send_text(const char *utf8)
{
    size_t len = link_utf8_truncate((const uint8_t *)utf8, strlen(utf8), LINK_MAX_TEXT);
    heltec_send(LINK_T_TEXT, (const uint8_t *)utf8, (uint16_t)len);
    pc_log("TEXT -> Heltec (%u B)", (unsigned)len);
}

void stt_link_send_cmd(uint16_t id)
{
    uint8_t p[2] = { (uint8_t)(id & 0xFFU), (uint8_t)(id >> 8) };
    heltec_send(LINK_T_CMD, p, sizeof p);
    pc_log("CMD %u -> Heltec", (unsigned)id);
}

bool stt_link_ptt_active(void)
{
    return ptt_down;
}

/*******************************************************************************
* Audio upload
*******************************************************************************/
static void upload_recording(size_t samples)
{
    uint8_t begin[6];
    uint32_t rate = STT_LINK_SAMPLE_RATE;
    begin[0] = (uint8_t)(rate & 0xFFU);
    begin[1] = (uint8_t)(rate >> 8);
    begin[2] = (uint8_t)(samples & 0xFFU);
    begin[3] = (uint8_t)(samples >> 8);
    begin[4] = (uint8_t)(samples >> 16);
    begin[5] = (uint8_t)(samples >> 24);
    pc_send(LINK_T_AUDIO_BEGIN, begin, sizeof begin);

    const uint8_t *bytes = (const uint8_t *)rec_buf;   /* little-endian int16 */
    size_t total = samples * sizeof(int16_t);
    for (size_t off = 0U; off < total; off += AUDIO_CHUNK_BYTES)
    {
        size_t n = total - off;
        if (n > AUDIO_CHUNK_BYTES) { n = AUDIO_CHUNK_BYTES; }
        pc_send(LINK_T_AUDIO_CHUNK, &bytes[off], (uint16_t)n);
    }
    pc_send(LINK_T_AUDIO_END, NULL, 0U);
}

/*******************************************************************************
* Incoming frames
*******************************************************************************/
static void on_pc_frame(const link_decoder_t *d)
{
    if (d->type == LINK_T_TEXT)
    {
        size_t len = link_utf8_truncate(d->payload, d->len, LINK_MAX_TEXT);
        heltec_send(LINK_T_TEXT, d->payload, (uint16_t)len);
    }
    else if ((d->type == LINK_T_CMD) && (d->len >= 2U))
    {
        heltec_send(LINK_T_CMD, d->payload, 2U);
    }
}

static void on_heltec_frame(const link_decoder_t *d)
{
    /* TX_RESULT and LOG are passed to the PC unchanged */
    if ((d->type == LINK_T_TX_RESULT) || (d->type == LINK_T_LOG) || (d->type == LINK_T_RX_TEXT))
    {
        pc_send(d->type, d->payload, d->len);
    }
}

static void poll_uart(CySCB_Type *hw, link_decoder_t *dec, void (*handler)(const link_decoder_t *))
{
    while (Cy_SCB_UART_GetNumInRxFifo(hw) > 0U)
    {
        if (link_decoder_push(dec, (uint8_t)Cy_SCB_UART_Get(hw)))
        {
            handler(dec);
        }
    }
}

/*******************************************************************************
* Task
*******************************************************************************/
static void stt_link_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    TickType_t press_tick = 0;

    pc_log("stt_link ready: hold USER BTN to talk (max %u s)", (unsigned)STT_LINK_MAX_SECONDS);

    for (;;)
    {
        bool pressed = (Cy_GPIO_Read(STT_LINK_PTT_PORT, STT_LINK_PTT_PIN) == PTT_PRESSED_LEVEL);

        if (pressed && !ptt_down)
        {
            rec_count = 0U;
            press_tick = xTaskGetTickCount();
            ptt_down = true;
        }
        else if (!pressed && ptt_down)
        {
            ptt_down = false;
            uint32_t held_ms = (uint32_t)((xTaskGetTickCount() - press_tick) * portTICK_PERIOD_MS);
            size_t samples = rec_count;
            if ((held_ms >= PTT_MIN_MS) && (samples > 0U))
            {
                pc_log("PTT %lu ms, %u samples", (unsigned long)held_ms, (unsigned)samples);
                upload_recording(samples);
            }
        }

        poll_uart(PC_UART_HW, &dec_pc, on_pc_frame);
        poll_uart(STT_LINK_HELTEC_HW, &dec_heltec, on_heltec_frame);

        vTaskDelayUntil(&last, pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

cy_rslt_t stt_link_start(uint32_t priority)
{
    link_decoder_init(&dec_pc);
    link_decoder_init(&dec_heltec);
    pc_tx_lock = xSemaphoreCreateMutex();
    heltec_tx_lock = xSemaphoreCreateMutex();
    if ((pc_tx_lock == NULL) || (heltec_tx_lock == NULL))
    {
        return STT_LINK_RSLT_ERR;
    }

    if (CY_SCB_UART_SUCCESS != Cy_SCB_UART_Init(STT_LINK_HELTEC_HW, &STT_LINK_HELTEC_CONFIG, &heltec_ctx))
    {
        printf("[STT] Heltec UART init failed\r\n");
        return STT_LINK_RSLT_ERR;
    }
    Cy_SCB_UART_Enable(STT_LINK_HELTEC_HW);

    if (pdPASS != xTaskCreate(stt_link_task, "stt_link", 1024U, NULL, priority, NULL))
    {
        return STT_LINK_RSLT_ERR;
    }
    return CY_RSLT_SUCCESS;
}
