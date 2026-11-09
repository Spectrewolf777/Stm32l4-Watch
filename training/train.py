"""Train the sound-event classifier and export it for STM32Cube.AI.

Data layout (one folder per class, any sample rate / mono or stereo wav|flac|ogg):

    data/raw_open/background/*.wav     <- REQUIRED: everything that must NOT alert
    data/raw_open/door_knock/*.wav
    data/raw_open/siren/*.wav
    data/raw_open/alarm/*.wav          <- smoke / fire / CO alarm beeps
    data/raw_open/<any_new_class>/...  <- e.g. doorbell, baby_cry - picked up automatically

Files whose names share the part before "__" (e.g. "rec-0815__1.wav",
"rec-0815__2.wav") are treated as the same recording and always land in the
same train/val/test split, so the test score is honest.

Usage:  python train.py            (reads data/raw_open/, outputs go to out_open/)
"""
import argparse
import glob
import os
from collections import defaultdict
from math import gcd

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

from config import SAMPLE_RATE, HOP, N_MELS, N_FRAMES, CLIP_LEN, BACKGROUND
import features

AUDIO_EXT = (".wav", ".flac", ".ogg")


# --------------------------------------------------------------------------- audio
def load_audio(path):
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    x = x.mean(axis=1)
    if sr != SAMPLE_RATE:
        g = gcd(sr, SAMPLE_RATE)
        x = resample_poly(x, SAMPLE_RATE // g, sr // g).astype(np.float32)
    return x


def windows_from_file(x, is_event, rng):
    """Cut a file into ~1 s windows. For event classes keep only the windows that
    actually contain the event (loud part), otherwise silent padding at the start
    of a 'knock' file would be learned as 'knock'."""
    if len(x) < CLIP_LEN:
        x = np.pad(x, (0, CLIP_LEN - len(x)))
    step = CLIP_LEN // 4 if is_event else CLIP_LEN // 2
    starts = np.arange(0, len(x) - CLIP_LEN + 1, step)
    wins = np.stack([x[s:s + CLIP_LEN] for s in starts])
    if is_event:
        # loudest 32 ms frame inside each window
        n = CLIP_LEN // HOP
        e = (wins[:, :n * HOP].reshape(len(wins), n, HOP) ** 2).mean(-1).max(-1)
        wins = wins[e >= 0.3 * e.max()]
    seconds = len(x) / SAMPLE_RATE
    cap = max(6, int(seconds * 2)) if is_event else max(4, int(seconds))
    if len(wins) > cap:
        wins = wins[rng.choice(len(wins), cap, replace=False)]
    return wins


def group_key(path):
    name = os.path.splitext(os.path.basename(path))[0]
    return name.split("__")[0]


def split_files(files, rng, val=0.1, test=0.1):
    groups = defaultdict(list)
    for f in files:
        groups[group_key(f)].append(f)
    keys = sorted(groups)
    rng.shuffle(keys)
    n_val = max(1, int(round(len(keys) * val)))
    n_test = max(1, int(round(len(keys) * test)))
    pick = lambda ks: [f for k in ks for f in groups[k]]
    return pick(keys[n_val + n_test:]), pick(keys[:n_val]), pick(keys[n_val:n_val + n_test])


# --------------------------------------------------------------------------- augmentation
def _peak_to(x, dbfs):
    p = np.abs(x).max()
    return x if p < 1e-9 else x * (10 ** (dbfs / 20) / p)


def _rms(x):
    return np.sqrt((x ** 2).mean() + 1e-12)


def augment(wins, bg_pool, rng, copies, is_event):
    """Random level (the real mic level differs a lot from internet recordings),
    mixing with background noise at random SNR, and a little mic self-noise."""
    out = []
    for w in wins:
        for _ in range(copies):
            if is_event:
                y = _peak_to(w, rng.uniform(-45, -3))
                if rng.random() < 0.8:
                    b = bg_pool[rng.integers(len(bg_pool))]
                    snr = rng.uniform(0, 20)
                    y = y + b * (_rms(y) / (_rms(b) * 10 ** (snr / 20)))
            else:
                y = _peak_to(w, rng.uniform(-60, -3))
                if rng.random() < 0.5:
                    b = bg_pool[rng.integers(len(bg_pool))]
                    y = y + _peak_to(b, rng.uniform(-60, -10))
            y = y + rng.normal(0, 10 ** (rng.uniform(-95, -70) / 20), CLIP_LEN)
            out.append(np.clip(y, -1, 1).astype(np.float32))
    return out


def quiet_room(n, rng):
    """Near-silent background examples (a quiet room is the most common input)."""
    out = []
    for _ in range(n):
        y = rng.normal(0, 1, CLIP_LEN)
        y = np.cumsum(y) * rng.uniform(0, 0.02) + y          # some low-frequency rumble
        y = y - y.mean()
        out.append((y / _rms(y) * 10 ** (rng.uniform(-90, -50) / 20)).astype(np.float32))
    return out


# --------------------------------------------------------------------------- dataset
def build_dataset(data_dir, aug_copies, seed):
    rng = np.random.default_rng(seed)
    classes = sorted(d for d in os.listdir(data_dir) if os.path.isdir(os.path.join(data_dir, d)))
    if BACKGROUND not in classes:
        raise SystemExit(f"need a '{BACKGROUND}' folder in {data_dir}")
    classes.remove(BACKGROUND)
    classes = [BACKGROUND] + classes                      # background is always index 0

    raw = {s: {} for s in ("train", "val", "test")}
    synth = {s: {} for s in ("train", "val", "test")}   # per window: came from a synthetic file?
    for c in classes:
        files = sorted(f for f in glob.glob(os.path.join(data_dir, c, "**", "*"), recursive=True)
                       if f.lower().endswith(AUDIO_EXT))
        if len(files) < 3:
            raise SystemExit(f"class '{c}' has only {len(files)} files, need at least 3")
        for split, fl in zip(("train", "val", "test"), split_files(files, rng)):
            if split == "test" and c == BACKGROUND:
                test_bg_files = fl
            ws = [windows_from_file(load_audio(f), c != BACKGROUND, rng) for f in fl]
            raw[split][c] = np.concatenate(ws) if ws else np.zeros((0, CLIP_LEN), np.float32)
            synth[split][c] = np.concatenate([np.full(len(w), "synth" in os.path.basename(f))
                                              for w, f in zip(ws, fl)]) if ws else np.zeros(0, bool)
        print(f"  {c:>14}: {len(files):5d} files -> windows "
              + " / ".join(f"{s} {len(raw[s][c])}" for s in raw))

    out, test_clip = {}, None
    for split in ("train", "val", "test"):
        bg_pool = raw[split][BACKGROUND]
        copies = aug_copies if split == "train" else 1
        X, y, real = [], [], []
        for ci, c in enumerate(classes):
            is_event = c != BACKGROUND
            n_cp = copies if is_event else max(1, copies // 3)
            a = augment(raw[split][c], bg_pool, rng, n_cp, is_event)
            real += (~np.repeat(synth[split][c], n_cp)).tolist()
            if not is_event:
                q = quiet_room(max(20, len(a) // 10), rng)
                a += q
                real += [False] * len(q)
            if split == "test" and is_event and test_clip is None and a:
                test_clip = a[0]                          # raw audio kept for the C test vector
            # features right away: raw audio is ~13x bigger and would not fit in RAM
            for s in range(0, len(a), 4096):
                X.append(features.log_mel(np.stack(a[s:s + 4096])))
            y += [ci] * len(a)
            del a
        out[split] = (np.concatenate(X), np.array(y, np.int32), np.array(real, bool))
    return classes, out, test_clip, test_bg_files


def false_alarms_per_hour(predict, mean, std, files, rng, thresholds=(0.7, 0.8, 0.9), smooth=3,
                          every=15, cooldown_s=8.0, gate=200 / 32768):
    """Play the held-out background files back to back, each at a random loudness, through
    the same decision logic as sound_detector.c (smoothing, threshold, cooldown, energy gate).
    Returns (hours, {threshold: (alerts per hour, {class: count})})."""
    x = np.concatenate([_peak_to(load_audio(f), rng.uniform(-40, -6)) for f in files]).astype(np.float32)
    hop_s = HOP / SAMPLE_RATE
    n_win = (len(x) - CLIP_LEN) // (every * HOP)
    probs, quiet = [], None
    for i in range(n_win):
        clip = x[i * every * HOP:i * every * HOP + CLIP_LEN]
        if np.abs(clip).max() < gate and quiet is not None:
            probs.append(quiet)                         # firmware: skip network, 100 % background
            continue
        p = predict(((features.log_mel(clip)[0] - mean) / std)[..., None].astype(np.float32))
        if quiet is None:
            quiet = np.eye(len(p), dtype=np.float32)[0]
            if np.abs(clip).max() < gate:
                p = quiet
        probs.append(p)
    hours = len(x) / SAMPLE_RATE / 3600
    out = {}
    for th in thresholds:
        last, per = {}, defaultdict(int)
        for i in range(smooth - 1, len(probs)):
            P = np.mean(probs[i - smooth + 1:i + 1], axis=0)
            c = 1 + int(P[1:].argmax())
            t = i * every * hop_s
            if P[c] >= th and t - last.get(c, -1e9) > cooldown_s:
                last[c] = t
                per[c] += 1
        out[th] = (sum(per.values()) / hours, dict(per))
    return hours, out


# --------------------------------------------------------------------------- model
def build_model(n_classes, arch="ds"):
    import tensorflow as tf
    L = tf.keras.layers
    inp = tf.keras.Input((N_FRAMES, N_MELS, 1))
    x = inp
    if arch == "ds":
        # depthwise-separable CNN (as used for keyword spotting on MCUs): far more channels
        # for the same compute as the plain CNN (~2.9 M MACs, ~30 k params)
        def bn_relu(x):
            return L.ReLU()(L.BatchNormalization()(x))
        x = bn_relu(L.Conv2D(32, 3, padding="same", use_bias=False)(x))
        x = L.MaxPool2D(2)(x)                                            # 15 x 20
        for i, f in enumerate((64, 128, 128)):
            x = bn_relu(L.DepthwiseConv2D(3, padding="same", use_bias=False)(x))
            x = bn_relu(L.Conv2D(f, 1, use_bias=False)(x))
            if i == 0:
                x = L.MaxPool2D(2)(x)                                    # 7 x 10
        x = L.GlobalAveragePooling2D()(x)
        x = L.Dropout(0.3)(x)
        return tf.keras.Model(inp, L.Dense(n_classes, activation="softmax")(x))
    for i, f in enumerate((16, 32, 64)):
        x = L.Conv2D(f, 3, padding="same", use_bias=False)(x)
        x = L.BatchNormalization()(x)
        x = L.ReLU()(x)
        if i < 2:
            x = L.MaxPool2D(2)(x)
    x = L.GlobalAveragePooling2D()(x)
    x = L.Dropout(0.3)(x)
    out = L.Dense(n_classes, activation="softmax")(x)
    return tf.keras.Model(inp, out)


def spec_augment(x, y):
    import tensorflow as tf
    t = tf.random.uniform([], 0, 5, tf.int32)
    t0 = tf.random.uniform([], 0, N_FRAMES - t + 1, tf.int32)
    f = tf.random.uniform([], 0, 6, tf.int32)
    f0 = tf.random.uniform([], 0, N_MELS - f + 1, tf.int32)
    tr = tf.range(N_FRAMES)[:, None, None]
    fr = tf.range(N_MELS)[None, :, None]
    mask = tf.logical_or((tr >= t0) & (tr < t0 + t), (fr >= f0) & (fr < f0 + f))
    return tf.where(mask, tf.zeros_like(x), x), y


def confusion(y_true, y_pred, classes):
    n = len(classes)
    cm = np.zeros((n, n), int)
    for t, p in zip(y_true, y_pred):
        cm[t, p] += 1
    w = max(len(c) for c in classes)
    lines = [" " * (w + 2) + " ".join(f"{c[:8]:>8}" for c in classes) + "   <- predicted"]
    for i, c in enumerate(classes):
        rec = cm[i, i] / max(1, cm[i].sum())
        lines.append(f"{c:>{w}}  " + " ".join(f"{v:8d}" for v in cm[i]) + f"   recall {rec:5.1%}")
    return "\n".join(lines)


# --------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="data/raw_open")
    ap.add_argument("--out", default="out_open")
    ap.add_argument("--epochs", type=int, default=80)
    ap.add_argument("--aug", type=int, default=8, help="augmented copies per event window")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--arch", default="ds", choices=("ds", "cnn"), help="ds = depthwise-separable CNN")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    print("Loading audio + computing log-mel features ...")
    classes, ds, test_clip, test_bg_files = build_dataset(args.data, args.aug, args.seed)
    (F_tr, y_tr, _), (F_va, y_va, _), (F_te, y_te, real_te) = ds["train"], ds["val"], ds["test"]
    mean = F_tr.mean(axis=(0, 1))
    std = F_tr.std(axis=(0, 1)) + 1e-3
    norm = lambda F: ((F - mean) / std)[..., None].astype(np.float32)
    X_tr, X_va, X_te = norm(F_tr), norm(F_va), norm(F_te)

    import tensorflow as tf
    tf.random.set_seed(args.seed)
    counts = np.bincount(y_tr, minlength=len(classes))
    # square-root balancing: rare classes still count more, but not so much that the model
    # learns to shout "event" at everything (every false buzz costs the user's trust)
    class_weight = {i: float(np.sqrt(counts.max() / max(1, c))) for i, c in enumerate(counts)}
    print("train examples per class:", dict(zip(classes, counts.tolist())))

    model = build_model(len(classes), args.arch)
    model.summary()
    model.compile(optimizer=tf.keras.optimizers.Adam(1e-3),
                  loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    train_ds = (tf.data.Dataset.from_tensor_slices((X_tr, y_tr)).shuffle(len(X_tr), seed=args.seed)
                .map(spec_augment, num_parallel_calls=tf.data.AUTOTUNE).batch(64).prefetch(2))
    model.fit(train_ds, validation_data=(X_va, y_va), epochs=args.epochs, class_weight=class_weight,
              callbacks=[tf.keras.callbacks.EarlyStopping("val_loss", patience=18, restore_best_weights=True),
                         tf.keras.callbacks.ReduceLROnPlateau("val_loss", factor=0.5, patience=5)],
              verbose=2)
    model.save(os.path.join(args.out, "model.h5"))

    print("\nFloat model, test set:")
    print(confusion(y_te, model.predict(X_te, verbose=0).argmax(1), classes))

    # ---- full-integer quantisation (int8 weights + activations, float in/out)
    rng = np.random.default_rng(0)
    rep_idx = rng.choice(len(X_tr), min(500, len(X_tr)), replace=False)

    def representative():
        for i in rep_idx:
            yield [X_tr[i:i + 1]]

    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = representative
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    tfl = conv.convert()
    tfl_path = os.path.join(args.out, "model_int8.tflite")
    open(tfl_path, "wb").write(tfl)

    interp = tf.lite.Interpreter(model_content=tfl)
    interp.allocate_tensors()
    i_in, i_out = interp.get_input_details()[0]["index"], interp.get_output_details()[0]["index"]

    def tfl_predict(x):
        interp.set_tensor(i_in, x[None])
        interp.invoke()
        return interp.get_tensor(i_out)[0]

    P_te = np.stack([tfl_predict(x) for x in X_te])
    acc = (P_te.argmax(1) == y_te).mean()
    print(f"\nINT8 TFLite model, test set (this is what runs on the STM32): accuracy {acc:.1%}")
    report = confusion(y_te, P_te.argmax(1), classes)
    print(report)
    # synthetic beeps/sirens are easy; this is the number that predicts real-world behaviour
    acc_real = (P_te.argmax(1)[real_te] == y_te[real_te]).mean()
    report_real = confusion(y_te[real_te], P_te.argmax(1)[real_te], classes)
    print(f"\nINT8, REAL recordings only (no synthetic clips): accuracy {acc_real:.1%}")
    print(report_real)

    hrs, fa = false_alarms_per_hour(tfl_predict, mean, std, test_bg_files, np.random.default_rng(2))
    fa_txt = f"false buzzes on {hrs:.2f} h of held-out background audio (firmware logic, 3-result average):\n" + \
        "\n".join(f"  threshold {th:.1f}: {r:5.1f} per hour  ("
                  + ", ".join(f"{classes[c]} {n}" for c, n in sorted(per.items())) + ")"
                  for th, (r, per) in fa.items())
    print("\n" + fa_txt)

    # ---- firmware files
    features.write_c_files(args.out, classes, mean, std)
    clip16 = np.clip(np.round(test_clip * 32768), -32768, 32767).astype(np.int16)
    f16 = norm(features.log_mel(clip16.astype(np.float32) / 32768.0))[0]
    features.write_test_vector(args.out, clip16, f16[..., 0], tfl_predict(f16))
    with open(os.path.join(args.out, "labels.txt"), "w") as f:
        f.write("\n".join(classes) + "\n")
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write(f"classes: {classes}\nint8 test accuracy: {acc:.4f}\n\n{report}\n"
                f"\nreal recordings only: {acc_real:.4f}\n\n{report_real}\n\n{fa_txt}\n")
    print(f"\nDone. Now run tools/update_model.sh in the firmware repo.")


if __name__ == "__main__":
    main()
