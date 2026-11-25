/*
 * link_frame.h - UART framing shared by E84, PC bridge and Heltec.
 *
 *   A5 5A | type(1) | seq(1) | len(2, LE) | payload(len) | crc16(2, LE)
 *
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over type..payload.
 * Plain C99, no allocation, no platform headers: builds for CM55, ESP32
 * (Arduino) and the host test.
 */
#ifndef LINK_FRAME_H
#define LINK_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_SOF0            0xA5u
#define LINK_SOF1            0x5Au
#define LINK_HDR_LEN         6u      /* SOF0 SOF1 type seq lenL lenH */
#define LINK_CRC_LEN         2u
#define LINK_MAX_PAYLOAD     1024u   /* AUDIO_CHUNK is the largest */
#define LINK_MAX_FRAME       (LINK_HDR_LEN + LINK_MAX_PAYLOAD + LINK_CRC_LEN)
#define LINK_MAX_TEXT        180u    /* fits one LoRa packet with 6 B header */

typedef enum
{
    LINK_T_TEXT        = 0x01, /* UTF-8 text                      */
    LINK_T_CMD         = 0x02, /* uint16 command id               */
    LINK_T_AUDIO_BEGIN = 0x10, /* uint16 rate, uint32 samples     */
    LINK_T_AUDIO_CHUNK = 0x11, /* int16 PCM, <= 1024 B            */
    LINK_T_AUDIO_END   = 0x12, /* empty                           */
    LINK_T_TX_RESULT   = 0x20, /* seq, status, toa_ms(2), retries */
    LINK_T_RX_TEXT     = 0x21, /* rssi(2) snr(1) src(1) text      */
    LINK_T_LOG         = 0x7F  /* ASCII                           */
} link_type_t;

typedef enum
{
    LINK_TX_OK_ACKED = 0,
    LINK_TX_NO_ACK   = 1,
    LINK_TX_RADIO_ERR = 2,
    LINK_TX_BUSY     = 3
} link_tx_status_t;

uint16_t link_crc16(const uint8_t *data, size_t len, uint16_t crc);

/* Encode one frame into out. Returns frame length, or 0 if it does not fit. */
size_t link_encode(uint8_t type, uint8_t seq, const uint8_t *payload,
                   uint16_t len, uint8_t *out, size_t out_size);

/* Byte-at-a-time decoder. */
typedef struct
{
    uint8_t  state;
    uint8_t  type;
    uint8_t  seq;
    uint16_t len;
    uint16_t pos;
    uint16_t crc_rx;
    uint32_t crc_errors;
    uint8_t  payload[LINK_MAX_PAYLOAD];
} link_decoder_t;

void link_decoder_init(link_decoder_t *d);

/* Feed one byte. Returns 1 when a complete, CRC-valid frame is ready in
 * d->type / d->seq / d->len / d->payload (valid until the next call). */
int link_decoder_push(link_decoder_t *d, uint8_t byte);

/* Cut UTF-8 text to at most max_bytes without splitting a character.
 * Returns the number of bytes to keep. */
size_t link_utf8_truncate(const uint8_t *s, size_t len, size_t max_bytes);

#ifdef __cplusplus
}
#endif

#endif /* LINK_FRAME_H */
