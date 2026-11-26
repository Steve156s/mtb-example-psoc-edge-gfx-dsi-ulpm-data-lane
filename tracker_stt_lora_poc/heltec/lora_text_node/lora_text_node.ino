/*
 * lora_text_node.ino - Heltec Wireless Tracker V2 (ESP32-S3 + SX1262) as a
 * LoRa text modem for the E84 STT POC. Flash the SAME sketch on both boards,
 * only NODE_ID differs.
 *
 *   E84 --UART1 (link frames)--> this node --LoRa--> other node --USB--> PC
 *
 * - UART1 (GPIO6 RX / GPIO17 TX, 115200) and USB Serial both accept link
 *   frames TEXT / CMD, so the radio link can be tested from a PC before the
 *   E84 is wired (pc/send_text.py).
 * - Every TEXT/CMD goes out with want-ack; up to 3 retries. The result is
 *   reported back as TX_RESULT on the port it came from.
 * - Every packet received is ACKed, de-duplicated by (src, seq), shown on the
 *   TFT, and printed on USB Serial. USB_PLAIN_TEXT 1 (default): a readable
 *   UTF-8 line "[RX] node 1 seq 3 RSSI -42 dBm SNR 9 dB | <text>" that any
 *   serial terminal shows (115200, UTF-8). USB_PLAIN_TEXT 0: binary RX_TEXT
 *   frames for pc/rx_viewer.py --frames.
 *
 * Libraries: RadioLib (>= 7.x), Adafruit ST7735 and ST7789, Adafruit GFX.
 * Board: "Heltec Wireless Tracker" or "ESP32S3 Dev Module", USB CDC On Boot = Enabled.
 *
 * PIN NOTE: SX1262 pins below are the Wireless Tracker V1.1 map (Heltec and
 * Meshtastic use the same on V2). Front-end (FEM) pins GPIO4/5/7 are from the
 * V2 pin table on espboards.dev. Check both against Heltec's V2 schematic.
 */
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "link_frame.h"
#include "lora_packet.h"

/* ---------- per-board settings ---------- */
#ifndef NODE_ID
#define NODE_ID          1        /* 1 = sender next to E84, 2 = receiver */
#endif
#define NET_ID           0x42     /* group id, must match on both nodes */
#ifndef USB_PLAIN_TEXT
#define USB_PLAIN_TEXT   1        /* 1 = readable lines on USB, 0 = RX_TEXT frames */
#endif

/* ---------- radio settings (Taiwan 920-925 MHz) ---------- */
#define LORA_FREQ_MHZ    923.2
#define LORA_BW_KHZ      125.0
#define LORA_SF          9
#define LORA_CR          5        /* 4/5 */
#define LORA_SYNC        0x12     /* private network */
#define LORA_POWER_DBM   10       /* bench test; SX1262 output before the FEM */
#define LORA_PREAMBLE    8
#define LORA_TCXO_V      1.8

#define ACK_TIMEOUT_MS   1500
#define MAX_RETRIES      3

/* ---------- pins ---------- */
#define PIN_LORA_NSS     8
#define PIN_LORA_SCK     9
#define PIN_LORA_MOSI    10
#define PIN_LORA_MISO    11
#define PIN_LORA_RST     12
#define PIN_LORA_BUSY    13
#define PIN_LORA_DIO1    14
#define PIN_FEM_POWER    7        /* VFEM_Ctrl: HIGH = FEM powered        */
#define PIN_FEM_CSD      4        /* PA_CSD: HIGH = FEM enabled           */
#define PIN_FEM_CTX      5        /* PA_CTX: HIGH = TX path, LOW = RX     */
#define PIN_VEXT         3        /* HIGH = TFT/GNSS rail on              */
#define PIN_TFT_CS       38
#define PIN_TFT_RST      39
#define PIN_TFT_DC       40
#define PIN_TFT_SCLK     41
#define PIN_TFT_MOSI     42
#define PIN_TFT_BL       21
#define PIN_E84_RX       6        /* Heltec RX  <- E84 TX */
#define PIN_E84_TX       17       /* Heltec TX  -> E84 RX */
#define E84_BAUD         115200

SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
SPIClass tftSpi(HSPI);
Adafruit_ST7735 tft(&tftSpi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

static link_decoder_t decE84, decUsb;
static volatile bool rxFlag = false;
static uint8_t txSeq = 0;
static uint8_t lastSeqFrom[256];
static bool    seenFrom[256];
static uint32_t rxCount = 0, txCount = 0, txFail = 0;

#if defined(ESP32)
ICACHE_RAM_ATTR
#endif
static void onRadioIrq(void) { rxFlag = true; }

/* ---------- helpers ---------- */
static void sendFrame(Stream &port, uint8_t type, uint8_t seq, const uint8_t *p, uint16_t len)
{
    static uint8_t out[LINK_HDR_LEN + 256 + LINK_CRC_LEN];
    size_t n = link_encode(type, seq, p, len, out, sizeof out);
    if (n) { port.write(out, n); }
}

static void logTo(Stream &port, const char *msg)
{
#if USB_PLAIN_TEXT
    if (&port == &Serial) { Serial.printf("[log] %s\r\n", msg); return; }
#endif
    sendFrame(port, LINK_T_LOG, 0, (const uint8_t *)msg, (uint16_t)strlen(msg));
}

static void showStatus(const char *line1, const char *line2)
{
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_ORANGE);
    tft.setCursor(2, 2);
    tft.printf("Node %d  tx%lu/%lu rx%lu", NODE_ID, (unsigned long)(txCount - txFail),
               (unsigned long)txCount, (unsigned long)rxCount);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(2, 20);
    tft.print(line1);
    tft.setCursor(2, 40);
    tft.print(line2);   /* non-ASCII (Chinese) shows as boxes; full text goes to USB */
}

static void startRx(void)
{
    rxFlag = false;
    radio.startReceive();
}

/* Wait for an ACK of seq from anyone; other packets that arrive meanwhile are
 * handled normally. */
/* Returns true if buf was an ACK addressed to us (seq in *ackedSeq). */
static bool handleAirPacket(const uint8_t *buf, size_t len, int16_t rssi, int8_t snr, uint8_t *ackedSeq);

static bool waitAck(uint8_t seq)
{
    uint32_t t0 = millis();
    while (millis() - t0 < ACK_TIMEOUT_MS)
    {
        if (rxFlag)
        {
            uint8_t buf[LP_MAX_LEN];
            rxFlag = false;
            size_t len = radio.getPacketLength();
            int st = radio.readData(buf, len);
            uint8_t acked = 0;
            bool gotAck = false;
            if (st == RADIOLIB_ERR_NONE)
            {
                gotAck = handleAirPacket(buf, len, (int16_t)radio.getRSSI(), (int8_t)radio.getSNR(), &acked);
            }
            radio.startReceive();
            if (gotAck && acked == seq) { return true; }
        }
        delay(1);
    }
    return false;
}

/* Send TEXT/CMD over LoRa with ACK + retries; report TX_RESULT to port. */
static void loraSend(Stream &port, uint8_t uartSeq, uint8_t lpType, const uint8_t *p, uint16_t len)
{
    uint8_t air[LP_MAX_LEN];
    uint8_t res[5];
    lp_packet_t pkt = { lpType, NET_ID, NODE_ID, LP_BROADCAST, txSeq, LP_F_WANT_ACK, p, (uint8_t)len };
    size_t n = lp_build(&pkt, air, sizeof air);
    uint8_t status = LINK_TX_RADIO_ERR;
    uint8_t tries = 0;
    uint32_t toa = 0;

    if (n == 0) { logTo(port, "payload too long"); return; }
    toa = radio.getTimeOnAir(n) / 1000;
    txCount++;
    for (tries = 0; tries <= MAX_RETRIES; tries++)
    {
        if (tries) { delay(random(100, 600)); }       /* random backoff */
        int st = radio.transmit(air, n);
        if (st != RADIOLIB_ERR_NONE) { status = LINK_TX_RADIO_ERR; break; }
        startRx();
        if (waitAck(pkt.seq)) { status = LINK_TX_OK_ACKED; break; }
        status = LINK_TX_NO_ACK;
    }
    if (status != LINK_TX_OK_ACKED) { txFail++; }
    txSeq++;
    startRx();

    res[0] = uartSeq;
    res[1] = status;
    res[2] = (uint8_t)(toa & 0xFF);
    res[3] = (uint8_t)(toa >> 8);
    res[4] = tries > MAX_RETRIES ? MAX_RETRIES : tries;
    sendFrame(port, LINK_T_TX_RESULT, uartSeq, res, sizeof res);
#if USB_PLAIN_TEXT
    if (&port != &Serial)
    {
        Serial.printf("[TX] seq %u %s, air %lu ms, retries %u | %.*s\r\n", (unsigned)pkt.seq,
                      status == LINK_TX_OK_ACKED ? "acked" : "NO ACK", (unsigned long)toa,
                      (unsigned)res[4], lpType == LP_T_CMD ? 3 : (int)len,
                      lpType == LP_T_CMD ? "CMD" : (const char *)p);
    }
#endif
    showStatus(status == LINK_TX_OK_ACKED ? "TX ok (acked)" : "TX no ack", "");
}

static void sendAck(uint8_t dst, uint8_t seq)
{
    uint8_t air[LP_HDR_LEN + 1];
    uint8_t s = seq;
    lp_packet_t ack = { LP_T_ACK, NET_ID, NODE_ID, dst, 0, 0, &s, 1 };
    size_t n = lp_build(&ack, air, sizeof air);
    radio.transmit(air, n);
}

static bool handleAirPacket(const uint8_t *buf, size_t len, int16_t rssi, int8_t snr, uint8_t *ackedSeq)
{
    lp_packet_t p;
    if (!lp_parse(buf, len, NET_ID, &p) || p.src == NODE_ID) { return false; }

    if (p.type == LP_T_ACK)
    {
        if (p.dst == NODE_ID && p.payload_len == 1 && ackedSeq) { *ackedSeq = p.payload[0]; return true; }
        return false;
    }
    if (p.flags & LP_F_WANT_ACK)
    {
        delay(20);                 /* let the sender switch to RX */
        sendAck(p.src, p.seq);
        startRx();
    }
    if (seenFrom[p.src] && lastSeqFrom[p.src] == p.seq) { return false; }   /* duplicate (our ACK was lost) */
    seenFrom[p.src] = true;
    lastSeqFrom[p.src] = p.seq;
    rxCount++;

    /* RX_TEXT to USB: rssi(2) snr(1) src(1) text */
    uint8_t out[4 + LP_MAX_LEN];
    out[0] = (uint8_t)(rssi & 0xFF);
    out[1] = (uint8_t)((uint16_t)rssi >> 8);
    out[2] = (uint8_t)snr;
    out[3] = p.src;
    uint16_t tl = p.payload_len;
    if (p.type == LP_T_CMD && p.payload_len >= 2)
    {
        /* CMD -> "#<id>" so the PC/display can map it to a phrase */
        tl = (uint16_t)snprintf((char *)&out[4], 16, "#%u", (unsigned)(p.payload[0] | (p.payload[1] << 8)));
    }
    else
    {
        memcpy(&out[4], p.payload, tl);
    }
#if USB_PLAIN_TEXT
    Serial.printf("[RX] node %u seq %u RSSI %d dBm SNR %d dB | %.*s\r\n", (unsigned)p.src,
                  (unsigned)p.seq, (int)rssi, (int)snr, (int)tl, (const char *)&out[4]);
#else
    sendFrame(Serial, LINK_T_RX_TEXT, p.seq, out, (uint16_t)(4 + tl));
#endif

    char l1[32], l2[64];
    snprintf(l1, sizeof l1, "from %u  %ddBm %ddB", p.src, rssi, snr);
    size_t cpy = tl < sizeof(l2) - 1 ? tl : sizeof(l2) - 1;
    memcpy(l2, &out[4], cpy);
    l2[cpy] = 0;
    showStatus(l1, l2);
    return false;
}

static void handleFrame(Stream &port, link_decoder_t &d)
{
    switch (d.type)
    {
        case LINK_T_TEXT:
        {
            size_t n = link_utf8_truncate(d.payload, d.len, LINK_MAX_TEXT);
            loraSend(port, d.seq, LP_T_TEXT, d.payload, (uint16_t)n);
            break;
        }
        case LINK_T_CMD:
            if (d.len >= 2) { loraSend(port, d.seq, LP_T_CMD, d.payload, 2); }
            break;
        default:
            break;      /* audio etc. is not for us */
    }
}

void setup(void)
{
    Serial.begin(115200);
    Serial1.begin(E84_BAUD, SERIAL_8N1, PIN_E84_RX, PIN_E84_TX);
    link_decoder_init(&decE84);
    link_decoder_init(&decUsb);

    pinMode(PIN_VEXT, OUTPUT);     digitalWrite(PIN_VEXT, HIGH);
    pinMode(PIN_TFT_BL, OUTPUT);   digitalWrite(PIN_TFT_BL, HIGH);
    pinMode(PIN_FEM_POWER, OUTPUT); digitalWrite(PIN_FEM_POWER, HIGH);
    pinMode(PIN_FEM_CSD, OUTPUT);   digitalWrite(PIN_FEM_CSD, HIGH);
    delay(50);

    tftSpi.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
    tft.initR(INITR_MINI160x80_PLUGIN);
    tft.setRotation(1);
    tft.setTextSize(1);

    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
    int st = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC,
                         LORA_POWER_DBM, LORA_PREAMBLE, LORA_TCXO_V, false);
    if (st != RADIOLIB_ERR_NONE)
    {
        char m[40];
        snprintf(m, sizeof m, "radio.begin err %d", st);
        logTo(Serial, m);
        showStatus("RADIO INIT FAIL", m);
        while (true) { delay(1000); }
    }
    radio.setDio2AsRfSwitch(true);
    radio.setRfSwitchPins(RADIOLIB_NC, PIN_FEM_CTX);   /* CTX high only while transmitting */
    radio.setCRC(true);
    radio.setPacketReceivedAction(onRadioIrq);
    randomSeed(NODE_ID * 7919u + micros());
    startRx();
    showStatus("ready", "923.2MHz SF9 BW125");
    logTo(Serial, "lora_text_node ready");
}

void loop(void)
{
    while (Serial1.available())
    {
        if (link_decoder_push(&decE84, (uint8_t)Serial1.read())) { handleFrame(Serial1, decE84); }
    }
    while (Serial.available())
    {
        if (link_decoder_push(&decUsb, (uint8_t)Serial.read())) { handleFrame(Serial, decUsb); }
    }
    if (rxFlag)
    {
        uint8_t buf[LP_MAX_LEN];
        rxFlag = false;
        size_t len = radio.getPacketLength();
        if (radio.readData(buf, len) == RADIOLIB_ERR_NONE)
        {
            handleAirPacket(buf, len, (int16_t)radio.getRSSI(), (int8_t)radio.getSNR(), NULL);
        }
        startRx();
    }
}
