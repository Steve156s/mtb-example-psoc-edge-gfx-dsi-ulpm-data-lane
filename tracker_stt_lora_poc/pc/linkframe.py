"""UART link frames shared with E84 and Heltec (see protocol/link_frame.h).

    A5 5A | type | seq | len(2 LE) | payload | crc16(2 LE)   CRC-16/CCITT-FALSE
"""
import struct

T_TEXT, T_CMD = 0x01, 0x02
T_AUDIO_BEGIN, T_AUDIO_CHUNK, T_AUDIO_END = 0x10, 0x11, 0x12
T_TX_RESULT, T_RX_TEXT, T_LOG = 0x20, 0x21, 0x7F
MAX_PAYLOAD = 1024
MAX_TEXT = 180
TX_STATUS = {0: "ok (acked)", 1: "no ack", 2: "radio error", 3: "busy"}


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(ftype: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too long")
    body = struct.pack("<BBH", ftype, seq & 0xFF, len(payload)) + payload
    return b"\xA5\x5A" + body + struct.pack("<H", crc16(body))


def utf8_truncate(text: str, max_bytes: int = MAX_TEXT) -> bytes:
    b = text.encode("utf-8")
    if len(b) <= max_bytes:
        return b
    n = max_bytes
    while n > 0 and (b[n] & 0xC0) == 0x80:
        n -= 1
    return b[:n]


class Decoder:
    """Feed bytes, get (type, seq, payload) tuples for CRC-valid frames."""

    def __init__(self):
        self.buf = bytearray()
        self.crc_errors = 0

    def feed(self, data: bytes):
        self.buf += data
        out = []
        while True:
            i = self.buf.find(b"\xA5\x5A")
            if i < 0:
                # keep a trailing A5 in case 5A arrives next
                self.buf = self.buf[-1:] if self.buf[-1:] == b"\xA5" else bytearray()
                return out
            del self.buf[:i]
            if len(self.buf) < 6:
                return out
            ftype, seq, ln = struct.unpack_from("<BBH", self.buf, 2)
            if ln > MAX_PAYLOAD:
                del self.buf[:1]
                continue
            total = 6 + ln + 2
            if len(self.buf) < total:
                return out
            body = bytes(self.buf[2:6 + ln])
            (crc_rx,) = struct.unpack_from("<H", self.buf, 6 + ln)
            if crc16(body) == crc_rx:
                out.append((ftype, seq, bytes(self.buf[6:6 + ln])))
                del self.buf[:total]
            else:
                self.crc_errors += 1
                del self.buf[:1]
