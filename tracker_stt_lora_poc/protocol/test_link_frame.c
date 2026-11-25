/* Host test: cc -std=c99 -Wall -Wextra -I. test_link_frame.c link_frame.c && ./a.out */
#include <stdio.h>
#include <string.h>
#include "link_frame.h"
#include "lora_packet.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int feed(link_decoder_t *d, const uint8_t *b, size_t n)
{
    int got = 0;
    for (size_t i = 0; i < n; i++) { got += link_decoder_push(d, b[i]); }
    return got;
}

int main(void)
{
    uint8_t buf[LINK_MAX_FRAME];
    link_decoder_t d;
    const char *txt = "\xE9\x9B\x86\xE5\x90\x88"; /* 集合 */

    /* CRC-16/CCITT-FALSE check value */
    CHECK(link_crc16((const uint8_t *)"123456789", 9, 0xFFFF) == 0x29B1);

    size_t n = link_encode(LINK_T_TEXT, 7, (const uint8_t *)txt, 6, buf, sizeof buf);
    CHECK(n == 14);
    link_decoder_init(&d);
    CHECK(feed(&d, buf, n) == 1);
    CHECK(d.type == LINK_T_TEXT && d.seq == 7 && d.len == 6 && memcmp(d.payload, txt, 6) == 0);

    /* garbage before, and a stray A5 right before SOF */
    uint8_t noisy[64] = { 0x00, 0xA5, 0x13, 0xA5 };
    memcpy(noisy + 4, buf, n);
    link_decoder_init(&d);
    CHECK(feed(&d, noisy, 4 + n) == 1);

    /* corrupted payload is rejected and counted */
    buf[8] ^= 0x01;
    link_decoder_init(&d);
    CHECK(feed(&d, buf, n) == 0 && d.crc_errors == 1);

    /* zero-length frame */
    n = link_encode(LINK_T_AUDIO_END, 1, NULL, 0, buf, sizeof buf);
    link_decoder_init(&d);
    CHECK(n == 8 && feed(&d, buf, n) == 1 && d.len == 0);

    /* max payload and oversize */
    static uint8_t big[LINK_MAX_PAYLOAD + 1];
    CHECK(link_encode(LINK_T_AUDIO_CHUNK, 2, big, LINK_MAX_PAYLOAD, buf, sizeof buf) == LINK_MAX_FRAME);
    CHECK(link_encode(LINK_T_AUDIO_CHUNK, 2, big, LINK_MAX_PAYLOAD + 1, buf, sizeof buf) == 0);

    /* UTF-8 truncation never splits a char: 集合 = 6 bytes */
    CHECK(link_utf8_truncate((const uint8_t *)txt, 6, 5) == 3);
    CHECK(link_utf8_truncate((const uint8_t *)txt, 6, 3) == 3);
    CHECK(link_utf8_truncate((const uint8_t *)txt, 6, 2) == 0);
    CHECK(link_utf8_truncate((const uint8_t *)"ab", 2, 10) == 2);

    /* LoRa packet round trip */
    uint8_t air[LP_MAX_LEN];
    lp_packet_t p = { LP_T_TEXT, 0x42, 1, LP_BROADCAST, 9, LP_F_WANT_ACK, (const uint8_t *)txt, 6 }, q;
    size_t an = lp_build(&p, air, sizeof air);
    CHECK(an == 12);
    CHECK(lp_parse(air, an, 0x42, &q) == 1 && q.type == LP_T_TEXT && q.seq == 9 && q.payload_len == 6);
    CHECK(lp_parse(air, an, 0x43, &q) == 0);

    printf(fails ? "%d FAILED\n" : "all link_frame tests passed\n", fails);
    return fails != 0;
}
