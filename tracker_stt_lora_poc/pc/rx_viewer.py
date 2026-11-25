#!/usr/bin/env python3
"""Receiver side: print what the receiving Heltec got (Chinese text, RSSI, SNR).

    python rx_viewer.py COM6
"""
import datetime
import struct
import sys

import serial  # pip install pyserial

import linkframe as lf

# CMD ids sent by route A (E84 on-device commands) -> phrase
COMMANDS = {
    1: "集合", 2: "等我", 3: "我卡住了", 4: "上纜車", 5: "我在纜車站", 6: "下山", 99: "SOS 求救",
}


def main():
    port = serial.Serial(sys.argv[1], 115200, timeout=0.1)
    dec = lf.Decoder()
    print("Waiting for LoRa messages...")
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
