#!/usr/bin/env python3
"""Receiver side: print what the receiving Heltec got (Chinese text, RSSI, SNR).

Default: the Heltec prints readable UTF-8 lines (USB_PLAIN_TEXT 1), so any
serial terminal works too (115200, UTF-8). This script adds a timestamp and
maps command ids (#3) to phrases.

    python rx_viewer.py COM6            # plain lines (default firmware)
    python rx_viewer.py COM6 --frames   # firmware built with USB_PLAIN_TEXT 0
"""
import datetime
import re
import struct
import sys

import serial  # pip install pyserial

import linkframe as lf

# CMD ids sent by route A (E84 on-device commands) -> phrase
COMMANDS = {
    1: "集合", 2: "等我", 3: "我卡住了", 4: "上纜車", 5: "我在纜車站", 6: "下山", 99: "SOS 求救",
}


def plain(port):
    buf = b""
    while True:
        buf += port.read(512)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", errors="replace").rstrip("\r")
            if not text:
                continue
            m = re.search(r"\| #(\d+)$", text)
            if m:
                text += f"  ({COMMANDS.get(int(m.group(1)), '未知指令')})"
            print(f"[{datetime.datetime.now():%H:%M:%S}] {text}", flush=True)


def main():
    port = serial.Serial(sys.argv[1], 115200, timeout=0.1)
    print("Waiting for LoRa messages...")
    if "--frames" not in sys.argv[2:]:
        plain(port)
    dec = lf.Decoder()
    while True:
        for ftype, seq, p in dec.feed(port.read(512)):
            now = datetime.datetime.now().strftime("%H:%M:%S")
            if ftype == lf.T_RX_TEXT and len(p) >= 4:
                rssi, snr, src = struct.unpack_from("<hbB", p)
                text = p[4:].decode("utf-8", errors="replace")
                if text.startswith("#") and text[1:].isdigit():
                    text = f"{COMMANDS.get(int(text[1:]), '未知指令')} ({text})"
                print(f"[{now}] node {src} seq {seq}  RSSI {rssi} dBm  SNR {snr} dB\n    {text}")
            elif ftype == lf.T_LOG:
                print(f"[{now}] [log] {p.decode(errors='replace')}")


if __name__ == "__main__":
    main()
