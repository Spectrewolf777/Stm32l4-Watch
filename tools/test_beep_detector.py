"""Test the smoke/CO beep detector (Core/Src/beep_detector.c) on the PC.

    training/.venv/bin/python tools/test_beep_detector.py [--fa-files N]

1. Detection: synthetic T3 (smoke) and T4 (CO) alarms with real-world timing tolerances,
   piezo harmonics and room reverb, mixed into real background audio at SNRs from -10 to +20 dB.
2. False alarms: all background recordings of the training project played back to back.
3. Real "alarm" clips from FSD50K (unknown pattern types; just shows what fires).
Check your own recordings (e.g. your real alarm recorded with the watch, label ALARM):
    training/.venv/bin/python tools/test_beep_detector.py --wav REC/ALARM_....WAV
"""
import argparse
import glob
import os
import subprocess
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DW = os.environ.get("TRAINING_DIR", os.path.join(ROOT, "training"))   # needs data/raw_open/
sys.path.insert(0, DW)
import train  # noqa: E402  (load_audio: any format -> 16 kHz mono float)

FS = 16000
BIN = os.path.join(os.environ.get("TMPDIR", "/tmp"), "beep_host_test")


def build():
    src = ["tools/beep_host.c", "Core/Src/beep_detector.c", "Core/Src/audio_features.c", "Core/AI/model_tables.c"]
    subprocess.run(["gcc", "-O2", "-I", "Core/Inc", "-I", "Core/AI", *src, "-lm", "-o", BIN], cwd=ROOT, check=True)


def run(x, verbose=False):
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
    out = subprocess.run([BIN] + (["-v"] if verbose else []), input=pcm, capture_output=True,
                         check=True).stdout.decode().split("\n")
    if verbose:
        print("\n".join(out))
    return [(l.split()[0], float(l.split()[1])) for l in out if l.startswith(("T3", "T4"))]


def alarm(kind, dur, rng):
    """kind T3 / T4 with +/-10 % timing, 2.9-3.5 kHz piezo with harmonics, some reverb."""
    t = np.arange(int(dur * FS)) / FS
    f0 = rng.uniform(2900, 3500)
    f = f0 * (1 + 0.003 * np.sin(2 * np.pi * rng.uniform(3, 30) * t))      # slight warble
    ph = 2 * np.pi * np.cumsum(f) / FS
    y = np.sin(ph) + rng.uniform(0.1, 0.5) * np.sign(np.sin(ph)) * 0.3     # piezo: some square-ish content
    j = lambda v: v * rng.uniform(0.9, 1.1)
    if kind == "T3":
        on, off, n, gap = j(0.5), j(0.5), 3, j(1.5)
    else:
        on, off, n, gap = j(0.1), j(0.1), 4, j(5.0)
    period = n * (on + off) - off + gap
    tp = t % period
    k = np.floor(tp / (on + off))
    env = ((k < n) & ((tp - k * (on + off)) < on)).astype(float)
    env = np.convolve(env, np.ones(40) / 40, mode="same")
    y = y * env
    rt = rng.uniform(0.1, 0.7)                                             # room reverb
    m = int(rt * FS)
    ir = rng.normal(0, 1, m) * np.exp(-6.9 * np.arange(m) / m) * 0.05
    ir[0] = 1.0
    return np.convolve(y, ir)[:len(y)], env


def bg_pool(n, rng):
    files = sorted(glob.glob(os.path.join(DW, "data/raw_open/background/*.wav")))
    pick = rng.choice(len(files), min(n, len(files)), replace=False)
    return [files[i] for i in pick]


def background(files, seconds, rng):
    parts, total = [], 0
    while total < seconds * FS:
        x = train.load_audio(files[rng.integers(len(files))])
        parts.append(x / (np.sqrt((x ** 2).mean()) + 1e-9))
        total += len(x)
    return np.concatenate(parts)[:int(seconds * FS)]


def detection_test(rng, files, trials, seconds=30):
    print(f"== detection (alarm starts at 2 s, {seconds + 2} s of audio; alert must be the right type)")
    print("   SNR = alarm (while beeping) vs background loudness")
    for kind in ("T3", "T4"):
        for snr in (-10, -5, 0, 5, 10, 20):
            hit, lat = 0, []
            for _ in range(trials):
                a, env = alarm(kind, seconds, rng)
                a = np.concatenate([np.zeros(2 * FS), a])
                b = background(files, len(a) / FS, rng)
                a_rms = np.sqrt((a[np.concatenate([np.zeros(2 * FS), env]) > 0.5] ** 2).mean())
                x = b + a * (10 ** (snr / 20) / a_rms)
                x = x / np.abs(x).max() * 10 ** (rng.uniform(-40, -6) / 20)
                ev = [e for e in run(x) if e[0] == kind]
                if ev:
                    hit += 1
                    lat.append(ev[0][1] - 2.0)
            print(f"   {kind}  SNR {snr:+3d} dB: detected {hit:3d}/{trials}"
                  + (f"   median {np.median(lat):4.1f} s after the first beep" if lat else ""))


def false_alarm_test(rng, n_files):
    files = sorted(glob.glob(os.path.join(DW, "data/raw_open/background/*.wav")))
    if n_files:
        files = [files[i] for i in rng.choice(len(files), min(n_files, len(files)), replace=False)]
    print(f"== false alarms: {len(files)} background recordings back to back")
    secs, events = 0.0, []
    for i in range(0, len(files), 150):
        xs = []
        for f in files[i:i + 150]:
            x = train.load_audio(f)
            xs.append(x / (np.abs(x).max() + 1e-9) * 10 ** (rng.uniform(-40, -6) / 20))
        x = np.concatenate(xs)
        events += [(e, files[i]) for e in run(x)]
        secs += len(x) / FS
    print(f"   {secs / 3600:.1f} h of audio: {len(events)} false alerts "
          f"({len(events) / (secs / 3600):.2f} per hour)")
    for (k, t), f in events[:10]:
        print(f"     {k} at {t:.1f} s in batch starting {os.path.basename(f)}")


def real_alarm_clips():
    files = [f for f in sorted(glob.glob(os.path.join(DW, "data/raw_open/alarm/*.wav"))) if "synth" not in f]
    print(f"== real 'alarm' clips ({len(files)}; mostly alarm clocks / beepers of unknown pattern)")
    hits = []
    for f in files:
        x = train.load_audio(f)
        if len(x) < 6 * FS:
            continue
        ev = run(x / (np.abs(x).max() + 1e-9) * 0.3)
        if ev:
            hits.append((os.path.basename(f), ev[0][0], len(x) / FS))
    long_ = sum(len(train.load_audio(f)) >= 6 * FS for f in files)
    print(f"   {long_} clips are >= 6 s (long enough for 2 cycles); {len(hits)} triggered:")
    for h in hits:
        print(f"     {h[1]}  {h[0]}  ({h[2]:.0f} s)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=30)
    ap.add_argument("--fa-files", type=int, default=0, help="0 = all background files")
    ap.add_argument("--wav", nargs="+", help="only run these recordings, printing every beep")
    a = ap.parse_args()
    rng = np.random.default_rng(0)
    build()
    if a.wav:
        for f in a.wav:
            print(f"== {f}")
            ev = run(train.load_audio(f), verbose=True)
            print("   ->", ", ".join(f"{k} alert at {t:.1f} s" for k, t in ev) or "no alert")
        return
    detection_test(rng, bg_pool(400, rng), a.trials)
    false_alarm_test(rng, a.fa_files)
    real_alarm_clips()


if __name__ == "__main__":
    main()
