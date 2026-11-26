/*
 * lora_packet.h - over-the-air LoRa packet (6 B header). Same bytes later on
 * E84 driving SX1262 directly.
 *
 *   ver_type(1) | net_id(1) | src(1) | dst(1) | seq(1) | flags(1) | payload
 */
#ifndef LORA_PACKET_H
#define LORA_PACKET_H

#include <stdint.h>
#include <stddef.h>

#define LP_VERSION        1u
#define LP_HDR_LEN        6u
#define LP_MAX_LEN        255u
#define LP_BROADCAST      0xFFu

#define LP_T_TEXT         1u
#define LP_T_CMD          2u
#define LP_T_ACK          3u

#define LP_F_WANT_ACK     0x01u

typedef struct
{
    uint8_t type;
    uint8_t net_id;
    uint8_t src;
    uint8_t dst;
    uint8_t seq;
    uint8_t flags;
    const uint8_t *payload;
    uint8_t payload_len;
} lp_packet_t;

static inline size_t lp_build(const lp_packet_t *p, uint8_t *out, size_t out_size)
{
    size_t n = LP_HDR_LEN + p->payload_len;
    if ((n > LP_MAX_LEN) || (n > out_size)) { return 0u; }
    out[0] = (uint8_t)((LP_VERSION << 4) | (p->type & 0x0Fu));
    out[1] = p->net_id;
    out[2] = p->src;
    out[3] = p->dst;
    out[4] = p->seq;
    out[5] = p->flags;
    for (uint8_t i = 0; i < p->payload_len; i++) { out[LP_HDR_LEN + i] = p->payload[i]; }
    return n;
}

/* Returns 1 if buf is a valid v1 packet for net_id (fills p, payload points into buf). */
static inline int lp_parse(const uint8_t *buf, size_t len, uint8_t net_id, lp_packet_t *p)
{
    if ((len < LP_HDR_LEN) || ((buf[0] >> 4) != LP_VERSION) || (buf[1] != net_id)) { return 0; }
    p->type = buf[0] & 0x0Fu;
    p->net_id = buf[1];
    p->src = buf[2];
    p->dst = buf[3];
    p->seq = buf[4];
    p->flags = buf[5];
    p->payload = &buf[LP_HDR_LEN];
    p->payload_len = (uint8_t)(len - LP_HDR_LEN);
    return 1;
}

#endif /* LORA_PACKET_H */
