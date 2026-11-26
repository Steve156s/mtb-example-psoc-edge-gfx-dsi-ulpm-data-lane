/* link_frame.c - see link_frame.h */
#include "link_frame.h"

enum { S_SOF0 = 0, S_SOF1, S_TYPE, S_SEQ, S_LEN0, S_LEN1, S_PAYLOAD, S_CRC0, S_CRC1 };

uint16_t link_crc16(const uint8_t *data, size_t len, uint16_t crc)
{
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t link_encode(uint8_t type, uint8_t seq, const uint8_t *payload,
                   uint16_t len, uint8_t *out, size_t out_size)
{
    size_t total = LINK_HDR_LEN + (size_t)len + LINK_CRC_LEN;
    if ((len > LINK_MAX_PAYLOAD) || (out_size < total) || ((len > 0u) && (payload == NULL)))
    {
        return 0u;
    }
    out[0] = LINK_SOF0;
    out[1] = LINK_SOF1;
    out[2] = type;
    out[3] = seq;
    out[4] = (uint8_t)(len & 0xFFu);
    out[5] = (uint8_t)(len >> 8);
    for (uint16_t i = 0; i < len; i++)
    {
        out[LINK_HDR_LEN + i] = payload[i];
    }
    uint16_t crc = link_crc16(&out[2], 4u + len, 0xFFFFu);
    out[LINK_HDR_LEN + len]      = (uint8_t)(crc & 0xFFu);
    out[LINK_HDR_LEN + len + 1u] = (uint8_t)(crc >> 8);
    return total;
}

void link_decoder_init(link_decoder_t *d)
{
    d->state = S_SOF0;
    d->type = 0u;
    d->seq = 0u;
    d->len = 0u;
    d->pos = 0u;
    d->crc_rx = 0u;
    d->crc_errors = 0u;
}

int link_decoder_push(link_decoder_t *d, uint8_t c)
{
    switch (d->state)
    {
        case S_SOF0:
            if (c == LINK_SOF0) { d->state = S_SOF1; }
            break;
        case S_SOF1:
            d->state = (c == LINK_SOF1) ? S_TYPE : ((c == LINK_SOF0) ? S_SOF1 : S_SOF0);
            break;
        case S_TYPE:
            d->type = c; d->state = S_SEQ;
            break;
        case S_SEQ:
            d->seq = c; d->state = S_LEN0;
            break;
        case S_LEN0:
            d->len = c; d->state = S_LEN1;
            break;
        case S_LEN1:
            d->len |= (uint16_t)c << 8;
            d->pos = 0u;
            if (d->len > LINK_MAX_PAYLOAD) { d->state = S_SOF0; }
            else { d->state = (d->len == 0u) ? S_CRC0 : S_PAYLOAD; }
            break;
        case S_PAYLOAD:
            d->payload[d->pos++] = c;
            if (d->pos >= d->len) { d->state = S_CRC0; }
            break;
        case S_CRC0:
            d->crc_rx = c; d->state = S_CRC1;
            break;
        case S_CRC1:
        {
            uint8_t hdr[4] = { d->type, d->seq, (uint8_t)(d->len & 0xFFu), (uint8_t)(d->len >> 8) };
            uint16_t crc;
            d->crc_rx |= (uint16_t)c << 8;
            d->state = S_SOF0;
            crc = link_crc16(hdr, 4u, 0xFFFFu);
            crc = link_crc16(d->payload, d->len, crc);
            if (crc == d->crc_rx) { return 1; }
            d->crc_errors++;
            break;
        }
        default:
            d->state = S_SOF0;
            break;
    }
    return 0;
}

size_t link_utf8_truncate(const uint8_t *s, size_t len, size_t max_bytes)
{
    size_t n;
    if (len <= max_bytes) { return len; }
    n = max_bytes;
    /* back up while s[n] is a continuation byte (10xxxxxx): the char starting
     * before n would be split */
    while ((n > 0u) && ((s[n] & 0xC0u) == 0x80u)) { n--; }
    return n;
}
