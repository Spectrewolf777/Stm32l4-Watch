"""Sort FSD50K (real-world, CC-licensed, ~24.5 GB download) into data/raw/<class>/.

    python prepare_fsd50k.py                 # uses data/downloads/fsd50k/
    python prepare_fsd50k.py --n-bg 5000     # how many background clips to take

Expects the Zenodo files (record 4060432) in data/downloads/fsd50k/:
    FSD50K.ground_truth.zip (extracted), FSD50K.dev_audio.z01..z05 + .zip,
    FSD50K.eval_audio.z01 + .zip
The split archives are merged once with `zip -s 0` (needs `zip`, ~24 GB extra disk;
delete FSD50K.*_audio.merged.zip afterwards if you need the space).

Label mapping (FSD50K uses the AudioSet ontology, labels include their parents):
    Knock                      -> door_knock
    Siren                      -> siren
    Doorbell                   -> doorbell          (new class)
    exactly ["Alarm"]          -> alarm             (generic beeping alarms / alarm clocks;
                                                     FSD50K has NO smoke-alarm label)
    anything with Alarm/Knock/Siren/Doorbell that is not used above (telephone rings,
    car horns, bicycle bells) is SKIPPED, so the model is not taught they are "safe".
    everything else            -> background (sampled evenly across leaf labels)

Audio is converted to 16 kHz mono 16-bit wav on the way in (training loads faster).
"""
import argparse
import csv
import os
import subprocess
import zipfile
from collections import defaultdict
from math import gcd

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

DL = "data/downloads/fsd50k"
RAW = "data/raw"
SR = 16000
EVENT_LABELS = {"Alarm", "Knock", "Siren", "Doorbell"}


def classify(labels):
    s = set(labels)
    if "Knock" in s:
        return "door_knock"
    if "Doorbell" in s:
        return "doorbell"
    if "Siren" in s:
        return "siren"
    if s == {"Alarm"}:
        return "alarm"
    if s & EVENT_LABELS:
        return None                      # ambiguous alarm-like sound: skip
    return "background"


def merged_zip(split):
    out = os.path.join(DL, f"FSD50K.{split}_audio.merged.zip")
    if not os.path.exists(out):
        src = os.path.join(DL, f"FSD50K.{split}_audio.zip")
        print(f"  merging split archive {src} -> {out} (one-off, a few minutes) ...")
        tmp = out[:-4] + ".part.zip"          # zip insists on a .zip extension
        subprocess.run(["zip", "-q", "-s", "0", src, "--out", tmp], check=True)
        os.rename(tmp, out)
    return out


def to_16k(data, sr):
    x = data.mean(axis=1) if data.ndim == 2 else data
    if sr != SR:
        g = gcd(sr, SR)
        x = resample_poly(x, SR // g, sr // g)
    return np.clip(x, -1, 1).astype(np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n-bg", type=int, default=5000, help="background clips to take")
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)

    gt = os.path.join(DL, "FSD50K.ground_truth")
    picks = defaultdict(list)                       # class -> [(split, fname)]
    bg_by_leaf = defaultdict(list)
    for split in ("dev", "eval"):
        for r in csv.DictReader(open(os.path.join(gt, f"{split}.csv"))):
            labels = r["labels"].split(",")
            c = classify(labels)
            if c == "background":
                bg_by_leaf[labels[0]].append((split, r["fname"]))
            elif c:
                picks[c].append((split, r["fname"]))

    # background: round-robin over leaf labels so speech/music don't swamp everything
    leaves = sorted(bg_by_leaf)
    for k in leaves:
        rng.shuffle(bg_by_leaf[k])
    bg, i = [], 0
    while len(bg) < a.n_bg and any(bg_by_leaf.values()):
        k = leaves[i % len(leaves)]
        if bg_by_leaf[k]:
            bg.append(bg_by_leaf[k].pop())
        i += 1
    picks["background"] = bg

    by_split = defaultdict(list)
    for c, items in picks.items():
        for split, fn in items:
            by_split[split].append((c, fn))
    for c in sorted(picks):
        print(f"  {c:>12}: {len(picks[c])} clips")

    n = 0
    for split, items in by_split.items():
        z = zipfile.ZipFile(merged_zip(split))
        names = {os.path.basename(m): m for m in z.namelist() if m.endswith(".wav")}
        for c, fn in items:
            dst = os.path.join(RAW, c, f"fsd50k-{split}{fn}__0.wav")
            if os.path.exists(dst):
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with z.open(names[f"{fn}.wav"]) as f:
                data, sr = sf.read(f, dtype="float32")
            sf.write(dst, to_16k(data, sr), SR, subtype="PCM_16")
            n += 1
            if n % 500 == 0:
                print(f"    {n} files written ...")
    print(f"  wrote {n} new files into {RAW}/")
    for c in sorted(os.listdir(RAW)):
        print(f"    {c:>12}: {len(os.listdir(os.path.join(RAW, c)))} files")


if __name__ == "__main__":
    main()
