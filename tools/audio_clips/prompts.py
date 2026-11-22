"""prompts.csv loader shared by tts_generate.py and wav2c.py."""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def load_prompts(path=os.path.join(HERE, "prompts.csv")):
    out = []
    with open(path, encoding="utf-8") as f:
        for row in csv.reader(f):
            if not row or row[0].lstrip().startswith("#"):
                continue
            out.append((row[0].strip(), row[1].strip()))
    return out
