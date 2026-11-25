#!/usr/bin/env python3
"""Phase 0: type text on the PC, send it to a Heltec node over USB, see the
TX_RESULT. Tests the LoRa link before the E84 is wired.

    python send_text.py COM5            (Windows)  /  /dev/ttyACM0 (Linux/macOS)
"""
import sys
import time

import serial  # pip install pyserial

import linkframe as lf


def main():
    port = serial.Serial(sys.argv[1], 115200, timeout=0.1)
    dec = lf.Decoder()
    seq = 0
    print("Type a line and press Enter (Ctrl+C to quit).")
    while True:
        line = input("> ").strip()
        if not line:
            continue
        payload = lf.utf8_truncate(line)
        port.write(lf.encode(lf.T_TEXT, seq, payload))
        t0 = time.time()
        done = False
        while time.time() - t0 < 10 and not done:
            for ftype, fseq, p in dec.feed(port.read(256)):
                if ftype == lf.T_TX_RESULT and len(p) >= 5:
                    toa = p[2] | (p[3] << 8)
                    print(f"  seq {p[0]}: {lf.TX_STATUS.get(p[1], p[1])}, air {toa} ms, retries {p[4]}, "
                          f"{(time.time() - t0) * 1000:.0f} ms total")
                    done = True
                elif ftype == lf.T_LOG:
                    print("  [log]", p.decode(errors="replace"))
        if not done:
            print("  no TX_RESULT within 10 s")
        seq = (seq + 1) & 0xFF


if __name__ == "__main__":
    main()
