"""Generate synthetic smoke/fire/CO alarm and siren recordings.

Smoke alarms are standardised (ISO 8201 / NFPA 72 "T3" pattern: 3 x 0.5 s beeps,
1.5 s pause, piezo tone ~2.9-3.5 kHz; CO alarms use "T4": 4 x 0.1 s beeps), so
synthetic examples are a good supplement. Sirens are approximated by wail, yelp
and hi-lo patterns with harmonics. Always ADD real recordings too - synthetic
data alone will not generalise well.

    python synth_alarms.py --n 150
"""
import argparse
import os

import numpy as np
import soundfile as sf

FS = 16000


def tone(freq_track, harmonics, rng):
    phase = 2 * np.pi * np.cumsum(freq_track) / FS
    y = np.zeros_like(phase)
    for h, a in enumerate(harmonics, start=1):
        if np.all(freq_track * h < FS / 2 - 200):
            y += a * np.sin(h * phase + rng.uniform(0, 2 * np.pi))
    return y


def room(y, rng):
    """Cheap reverb: exponentially decaying noise impulse response."""
    rt = rng.uniform(0.05, 0.6)
    n = int(rt * FS)
    ir = rng.normal(0, 1, n) * np.exp(-6.9 * np.arange(n) / n)
    ir[0] = rng.uniform(1, 4)
    wet = np.convolve(y, ir)[:len(y)]
    return wet / (np.abs(wet).max() + 1e-9)


def smoke_alarm(dur, rng):
    t = np.arange(int(dur * FS)) / FS
    f0 = rng.uniform(2900, 3500)
    f = f0 * (1 + 0.004 * np.sin(2 * np.pi * rng.uniform(5, 40) * t))
    y = tone(f, [1, rng.uniform(0, 0.4)], rng)
    if rng.random() < 0.7:     # T3
        on, off, n, gap = rng.uniform(0.45, 0.55), rng.uniform(0.45, 0.55), 3, rng.uniform(1.4, 1.6)
    else:                      # T4 (CO) / fast beeping
        on, off, n, gap = rng.uniform(0.08, 0.12), rng.uniform(0.08, 0.12), 4, rng.uniform(4.5, 5.5)
    period = n * (on + off) - off + gap
    tp = (t + rng.uniform(0, period)) % period
    k = np.floor(tp / (on + off))
    env = ((k < n) & ((tp - k * (on + off)) < on)).astype(float)
    env = np.convolve(env, np.ones(48) / 48, mode="same")      # soft edges
    return y * env


def siren(dur, rng):
    t = np.arange(int(dur * FS)) / FS
    kind = rng.choice(["wail", "yelp", "hilo"])
    if kind == "wail":
        lo, hi, p = rng.uniform(500, 800), rng.uniform(1200, 1700), rng.uniform(2.5, 5)
        f = lo + (hi - lo) * (0.5 - 0.5 * np.cos(2 * np.pi * t / p))
    elif kind == "yelp":
        lo, hi, p = rng.uniform(500, 800), rng.uniform(1200, 1700), rng.uniform(0.15, 0.35)
        f = lo + (hi - lo) * ((t / p) % 1.0)
    else:
        a, b, p = rng.uniform(400, 500), rng.uniform(550, 750), rng.uniform(0.8, 1.2)
        f = np.where((t % p) < p / 2, a, b)
        f = np.convolve(f, np.ones(80) / 80, mode="same")
    f = f * (1 + rng.uniform(-0.04, 0.04) * t / dur)            # Doppler-ish drift
    y = tone(f, [1, rng.uniform(0.2, 0.6), rng.uniform(0.1, 0.4), rng.uniform(0, 0.2)], rng)
    return y * (0.3 + 0.7 * np.clip(t / rng.uniform(0.5, dur), 0, 1))   # approaching


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=150)
    ap.add_argument("--out", default="data/raw")
    a = ap.parse_args()
    rng = np.random.default_rng(42)
    for cls, fn in (("alarm", smoke_alarm), ("siren", siren)):
        os.makedirs(os.path.join(a.out, cls), exist_ok=True)
        for i in range(a.n):
            y = room(fn(rng.uniform(4, 8), rng), rng) * 0.7
            sf.write(os.path.join(a.out, cls, f"synth-{cls}-{i:04d}__0.wav"), y.astype(np.float32), FS)
    print(f"wrote {a.n} synthetic alarm and {a.n} synthetic siren files to {a.out}/")


if __name__ == "__main__":
    main()
