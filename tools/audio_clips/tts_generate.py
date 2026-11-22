#!/usr/bin/env python3
"""Synthesize the voice prompts in prompts.csv to 16 kHz mono 16-bit WAV files.

Offline TTS with sherpa-onnx + the MIT-licensed MeloTTS zh_en VITS model
(https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models,
 vits-melo-tts-zh_en.tar.bz2). Nothing is sent to an online service.

    python3 -m venv .venv && .venv/bin/pip install sherpa-onnx numpy
    .venv/bin/python tts_generate.py --model vits-melo-tts-zh_en --out wav

Requires ffmpeg on PATH for resampling 44.1 kHz -> 16 kHz.
This is a placeholder voice for the prototype: for the product, replace the
WAV files with studio recordings (same file names) and rerun wav2c.py.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np
import sherpa_onnx

from prompts import load_prompts


def _segments(s, sr, floor_db=-35.0, min_gap_s=0.06):
    """Voiced segments [(start, end)] split at pauses of at least min_gap_s."""
    f = int(sr * 0.01)
    n = len(s) // f
    if n == 0:
        return []
    e = np.array([np.sqrt(np.mean(s[i * f:(i + 1) * f] ** 2)) for i in range(n)])
    on = e > e.max() * 10 ** (floor_db / 20)
    out, start, last, gap = [], None, 0, 0
    for i, v in enumerate(on):
        if v:
            start = i if start is None else start
            last, gap = i, 0
        elif start is not None:
            gap += 1
            if gap >= int(min_gap_s / 0.01):
                out.append((start * f, (last + 1) * f))
                start = None
    if start is not None:
        out.append((start * f, (last + 1) * f))
    return out


def synth_single(tts, ch, sid):
    """Single syllables come out weak or empty from the VITS model when spoken
    alone. Say the syllable three times in a carrier ("X，X，X。") with two
    speeds and keep the loudest isolated segment of plausible length."""
    best, best_rms, sr = None, -1.0, 44100
    for text in (f"{ch}，{ch}，{ch}。", f"{ch}、{ch}、{ch}。"):
        for speed in (0.9, 1.0):
            a = tts.generate(text, sid=sid, speed=speed)
            s = np.asarray(a.samples, dtype=np.float64)
            sr = a.sample_rate
            for x, y in _segments(s, sr):
                if not 0.15 <= (y - x) / sr <= 0.5:
                    continue
                rms = float(np.sqrt(np.mean(s[x:y] ** 2)))
                if rms > best_rms:
                    pad = int(0.02 * sr)
                    best, best_rms = s[max(0, x - pad):y + pad], rms
    if best is None:
        a = tts.generate(ch, sid=sid, speed=1.0)
        best, sr = np.asarray(a.samples, dtype=np.float64), a.sample_rate
    return best, sr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="vits-melo-tts-zh_en directory")
    ap.add_argument("--out", default="wav")
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--sid", type=int, default=0)
    a = ap.parse_args()

    d = a.model.rstrip("/") + "/"
    cfg = sherpa_onnx.OfflineTtsConfig(
        model=sherpa_onnx.OfflineTtsModelConfig(
            vits=sherpa_onnx.OfflineTtsVitsModelConfig(
                model=d + "model.onnx", lexicon=d + "lexicon.txt",
                tokens=d + "tokens.txt", dict_dir=d + "dict"),
            num_threads=2))  # no rule FSTs: number.fst rewrites single digits and empties them
    tts = sherpa_onnx.OfflineTts(cfg)
    os.makedirs(a.out, exist_ok=True)

    for pid, text in load_prompts():
        if len(text) == 1:
            samples, rate = synth_single(tts, text, a.sid)
        else:
            audio = tts.generate(text, sid=a.sid, speed=a.speed)
            samples, rate = np.asarray(audio.samples), audio.sample_rate
        pcm = np.clip(samples * 32767.0, -32768, 32767).astype("<i2")
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            raw = tmp.name
        with wave.open(raw, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(pcm.tobytes())
        dst = os.path.join(a.out, pid + ".wav")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", raw,
                        "-ar", "16000", "-ac", "1", "-sample_fmt", "s16", dst], check=True)
        os.unlink(raw)
        print(f"{pid:18s} {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
