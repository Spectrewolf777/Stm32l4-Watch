"""Copy labelled recordings from the watch into data/raw/<class>/.

On the watch: Voice page -> tap the label button at the top until it shows what you are
about to record (ALARM, KNOCK, BELL, SIREN, BG), then record. Files are saved as
REC/KNOCK_261108_113000.WAV etc. Plug the watch into USB, then:

    python import_watch_recordings.py /run/media/$USER/<watch drive>/REC
    python import_watch_recordings.py /run/media/$USER/<watch drive>/REC --move   # also delete from watch

Unlabelled voice memos (R...WAV) are skipped. Each recording keeps its own name, so the
training split never puts parts of one recording in both train and test.
"""
import argparse
import os
import shutil

import numpy as np
import soundfile as sf

PREFIX_TO_CLASS = {
    "ALARM": "alarm",
    "KNOCK": "door_knock",
    "BELL": "doorbell",
    "SIREN": "siren",
    "BG": "background",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rec_dir", help="the REC folder on the watch's USB drive")
    ap.add_argument("--data", default="data/raw")
    ap.add_argument("--move", action="store_true", help="delete from the watch after copying")
    a = ap.parse_args()

    counts, seconds = {}, {}
    for name in sorted(os.listdir(a.rec_dir)):
        if not name.upper().endswith(".WAV"):
            continue
        prefix = name.split("_", 1)[0].upper()
        cls = PREFIX_TO_CLASS.get(prefix)
        src = os.path.join(a.rec_dir, name)
        if cls is None:
            print(f"  skip  {name}  (no label)")
            continue
        try:
            x, sr = sf.read(src, dtype="float32")
        except Exception as e:                       # half-written file (battery died, ...)
            print(f"  skip  {name}  (unreadable: {e})")
            continue
        dur = len(x) / sr
        if dur < 1.0:
            print(f"  skip  {name}  (only {dur:.1f} s)")
            continue
        peak = 20 * np.log10(np.abs(x).max() + 1e-9)
        dst_dir = os.path.join(a.data, cls)
        os.makedirs(dst_dir, exist_ok=True)
        dst = os.path.join(dst_dir, f"watch-{os.path.splitext(name)[0].lower()}__0.wav")
        shutil.copyfile(src, dst)
        if a.move:
            os.remove(src)
        warn = "  <- very quiet, was the sound close enough?" if peak < -40 and cls != "background" else ""
        warn = "  <- CLIPPING, sound too loud / gain too high" if peak > -0.5 else warn
        print(f"  {cls:>11}  {name}  {dur:6.1f} s  peak {peak:5.1f} dBFS{warn}")
        counts[cls] = counts.get(cls, 0) + 1
        seconds[cls] = seconds.get(cls, 0) + dur

    print("\nimported:")
    for c in sorted(counts):
        print(f"  {c:>11}: {counts[c]} recordings, {seconds[c] / 60:.1f} min")
    print("\nNext: python prepare_open_dataset.py && python train.py   then: tools/update_model.sh")


if __name__ == "__main__":
    main()
