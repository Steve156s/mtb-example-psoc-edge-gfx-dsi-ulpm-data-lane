#!/usr/bin/env python3
"""Route C: PC-side STT for the E84 (offline, no internet needed after the
model is downloaded once).

E84 --AUDIO_BEGIN/CHUNK/END (KitProg3 USB-UART)--> this script
this script --faster-whisper--> text --OpenCC s2twp--> TEXT frame --> E84
E84 forwards TEXT to Heltec and reports TX_RESULT, which is printed here.

    pip install pyserial faster-whisper opencc-python-reimplemented numpy
    python stt_bridge.py COM4 --baud 921600 --model small
    python stt_bridge.py COM4 --typed      # no STT: type text, E84 forwards it
    python stt_bridge.py COM4 --pc-mic     # STT from the PC microphone (Enter to
                                           # start/stop), before the E84 mic works
                                           # needs: pip install sounddevice
"""
import argparse
import os
import struct
import sys
import threading
import time
import wave

import numpy as np
import serial

import linkframe as lf


def load_stt(model_name):
    from faster_whisper import WhisperModel
    try:
        from opencc import OpenCC
        to_trad = OpenCC("s2twp").convert
    except ImportError:
        to_trad = lambda s: s  # noqa: E731
    print(f"Loading faster-whisper '{model_name}' (first run downloads the model)...")
    model = WhisperModel(model_name, device="cpu", compute_type="int8")

    def run(pcm16: np.ndarray, rate: int) -> str:
        audio = pcm16.astype(np.float32) / 32768.0
        if rate != 16000:
            idx = np.linspace(0, len(audio) - 1, int(len(audio) * 16000 / rate))
            audio = np.interp(idx, np.arange(len(audio)), audio).astype(np.float32)
        segments, _ = model.transcribe(audio, language="zh", beam_size=1, vad_filter=True,
                                       initial_prompt="以下是繁體中文的滑雪對話。")
        return to_trad("".join(s.text for s in segments).strip())

    return run


def save_wav(path, pcm16, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm16.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--model", default="small", help="tiny / base / small / medium")
    ap.add_argument("--typed", action="store_true", help="skip STT, type text instead")
    ap.add_argument("--pc-mic", action="store_true", help="record from the PC microphone instead of the E84")
    ap.add_argument("--wav-dir", default="recordings")
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0.05)
    lock = threading.Lock()
    seq = [0]

    def send_text(text):
        payload = lf.utf8_truncate(text)
        with lock:
            port.write(lf.encode(lf.T_TEXT, seq[0], payload))
            seq[0] = (seq[0] + 1) & 0xFF
        print(f"  -> E84 TEXT ({len(payload)} B): {payload.decode()}")

    stt = None if args.typed else load_stt(args.model)
    os.makedirs(args.wav_dir, exist_ok=True)

    if args.pc_mic:
        def pc_mic_loop():
            import sounddevice as sd
            while True:
                input("Press Enter to start talking...")
                frames = []
                with sd.InputStream(samplerate=16000, channels=1, dtype="int16",
                                    callback=lambda d, n, t, st: frames.append(d.copy())):
                    input("  recording... press Enter to stop")
                pcm = np.concatenate(frames)[:, 0] if frames else np.zeros(0, dtype=np.int16)
                wav = os.path.join(args.wav_dir, time.strftime("pcmic-%Y%m%d-%H%M%S.wav"))
                save_wav(wav, pcm, 16000)
                t0 = time.time()
                text = stt(pcm, 16000) if len(pcm) else ""
                print(f"  STT {time.time() - t0:.1f} s: {text!r}")
                if text:
                    send_text(text)
        threading.Thread(target=pc_mic_loop, daemon=True).start()
    elif args.typed:
        def typer():
            while True:
                line = sys.stdin.readline().strip()
                if line:
                    send_text(line)
        threading.Thread(target=typer, daemon=True).start()
        print("Typed mode: enter text lines.")
    if not (args.typed or args.pc_mic):
        print("Ready. Hold the PTT button on the E84, speak, release.")

    dec = lf.Decoder()
    rate, expected, chunks, t_begin = 16000, 0, [], 0.0
    while True:
        data = port.read(4096)
        if not data:
            continue
        for ftype, fseq, p in dec.feed(data):
            if ftype == lf.T_AUDIO_BEGIN and len(p) >= 6:
                rate, expected = struct.unpack_from("<HI", p)
                chunks, t_begin = [], time.time()
                print(f"Audio: {expected} samples @ {rate} Hz ({expected / rate:.1f} s)")
            elif ftype == lf.T_AUDIO_CHUNK:
                chunks.append(p)
            elif ftype == lf.T_AUDIO_END:
                pcm = np.frombuffer(b"".join(chunks), dtype="<i2")
                t_rx = time.time() - t_begin
                if len(pcm) != expected:
                    print(f"  warning: got {len(pcm)} of {expected} samples (CRC errors so far {dec.crc_errors})")
                wav = os.path.join(args.wav_dir, time.strftime("%Y%m%d-%H%M%S.wav"))
                save_wav(wav, pcm, rate)
                peak = int(np.abs(pcm).max()) if len(pcm) else 0
                print(f"  received in {t_rx:.1f} s, peak {20 * np.log10(max(peak, 1) / 32768):.1f} dBFS, saved {wav}")
                if stt and len(pcm):
                    t0 = time.time()
                    text = stt(pcm, rate)
                    print(f"  STT {time.time() - t0:.1f} s: {text!r}")
                    if text:
                        send_text(text)
            elif ftype == lf.T_TX_RESULT and len(p) >= 5:
                toa = p[2] | (p[3] << 8)
                print(f"  LoRa seq {p[0]}: {lf.TX_STATUS.get(p[1], p[1])}, air {toa} ms, retries {p[4]}")
            elif ftype == lf.T_LOG:
                print("  [E84]", p.decode(errors="replace"))


if __name__ == "__main__":
    main()
