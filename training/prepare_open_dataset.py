"""Build data/raw_open/: training data with no non-commercial licences.

Keeps only FSD50K clips licensed CC0 or CC-BY, plus synthetic alarms and recordings made with
the watch. Leaves out ESC-50 (CC BY-NC) and FSD50K clips under CC BY-NC or Sampling+.
Writes ATTRIBUTION_FSD50K.csv, the credits that CC-BY requires.

    python prepare_open_dataset.py [--n-bg 6800]
    python train.py --data data/raw_open --out out_open

Needs data/raw/ and the merged zips from prepare_fsd50k.py, plus FSD50K.metadata.zip unpacked
in data/downloads/fsd50k/meta/.
"""
import argparse
import csv
import glob
import json
import os
import shutil
import zipfile
from collections import Counter, defaultdict

import numpy as np
import soundfile as sf

from prepare_fsd50k import DL, RAW, SR, classify, merged_zip, to_16k

OPEN_DIR = "data/raw_open"
ALLOWED = ("publicdomain/zero/1.0", "licenses/by/3.0")


def link(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if not os.path.exists(dst):
        os.link(src, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n-bg", type=int, default=6800)
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)
    if os.path.isdir(OPEN_DIR):
        shutil.rmtree(OPEN_DIR)

    info = {}
    for split in ("dev", "eval"):
        path = glob.glob(os.path.join(DL, "meta", "**", f"{split}_clips_info_FSD50K.json"), recursive=True)[0]
        for fn, v in json.load(open(path)).items():
            info[(split, fn)] = v

    picks, bg_by_leaf = defaultdict(list), defaultdict(list)
    skipped = Counter()
    for split in ("dev", "eval"):
        for r in csv.DictReader(open(os.path.join(DL, "FSD50K.ground_truth", f"{split}.csv"))):
            labels = r["labels"].split(",")
            c = classify(labels)
            if c is None:
                continue
            lic = info[(split, r["fname"])]["license"]
            if not any(x in lic for x in ALLOWED):
                skipped[c] += 1
                continue
            if c == "background":
                bg_by_leaf[labels[0]].append((split, r["fname"]))
            else:
                picks[c].append((split, r["fname"]))

    leaves = sorted(bg_by_leaf)
    for k in leaves:
        rng.shuffle(bg_by_leaf[k])
    i = 0
    while len(picks["background"]) < a.n_bg and any(bg_by_leaf.values()):
        k = leaves[i % len(leaves)]
        if bg_by_leaf[k]:
            picks["background"].append(bg_by_leaf[k].pop())
        i += 1

    zips, names, rows, extracted = {}, {}, [], 0
    for c, items in sorted(picks.items()):
        for split, fn in items:
            base = f"fsd50k-{split}{fn}__0.wav"
            src, dst = os.path.join(RAW, c, base), os.path.join(OPEN_DIR, c, base)
            if os.path.exists(src):
                link(src, dst)
            else:
                if split not in zips:
                    zips[split] = zipfile.ZipFile(merged_zip(split))
                    names[split] = {os.path.basename(m): m for m in zips[split].namelist() if m.endswith(".wav")}
                with zips[split].open(names[split][f"{fn}.wav"]) as f:
                    data, sr = sf.read(f, dtype="float32")
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                sf.write(dst, to_16k(data, sr), SR, subtype="PCM_16")
                extracted += 1
            v = info[(split, fn)]
            rows.append([c, fn, f"https://freesound.org/s/{fn}/", v["title"], v["uploader"], v["license"]])

    kept = Counter()
    for c in os.listdir(RAW):
        for n in os.listdir(os.path.join(RAW, c)):
            if n.startswith(("synth-", "watch-")):
                link(os.path.join(RAW, c, n), os.path.join(OPEN_DIR, c, n))
                kept[n.split("-")[0]] += 1

    with open("ATTRIBUTION_FSD50K.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["class", "freesound_id", "url", "title", "uploader", "license"])
        w.writerows(rows)
    print(f"non-commercial/Sampling+ clips left out, by class: {dict(skipped)}")
    print(f"extracted {extracted} extra background clips; kept {dict(kept)}")
    for c in sorted(os.listdir(OPEN_DIR)):
        print(f"  {c:>11}: {len(os.listdir(os.path.join(OPEN_DIR, c)))} files")


if __name__ == "__main__":
    main()
