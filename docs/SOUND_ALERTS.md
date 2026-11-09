# Sound alerts (AI)

The watch listens with its PDM mic and alerts (vibration + popup that wakes the screen) when it
hears a **smoke/fire alarm, door knock, doorbell or siren**. The network is trained on a PC in the
[`training/`](../training/) folder and runs on the STM32L4P5 with ST Edge AI.

```
mic.c (16 kHz) ─┬─> recorder.c   -> REC/*.WAV on flash (same samples!)
                └─> sound_detector.c -> audio_features.c (log-mel, 32 ms hops)
                                     -> Core/AI/network.c (int8 CNN, every 0.48 s)
                                     -> average of 3 results > 85 % -> sd_on_detect() in ui_watch.c
```

## Model results

Trained only on openly licensed data (see `THIRD_PARTY.md`). Held-out real recordings, per ~1 s
window: overall 85.0 %, background 92.0 %, doorbell 78.7 %, door knock 67.9 %, siren 54.7 %,
alarm 42.5 %. A sound lasting several seconds gets repeated chances. On 1.6 h of dense, varied
background audio, the firmware's decision logic gave 14 false alerts per hour at threshold 0.9 and
27 at 0.8 (the firmware uses 0.85). Recordings made through the watch are the main next step.

## Smoke / CO beep-pattern detector (no AI)

`beep_detector.c` runs next to the network on the same spectrum. Smoke and CO alarms use
standard beep rhythms (ISO 8201 / NFPA 72) on a ~3 kHz tone:

* **T3 smoke/fire**: 3 x (0.5 s beep, 0.5 s pause), 1.5 s pause, repeat -> alert after 2 cycles
  (median 7.5 s after the first beep)
* **T4 carbon monoxide**: 4 x (0.1 s beep, 0.1 s pause), ~5 s pause -> alert after 3 cycles on
  the same tone (median 13 s)

It needs no training and copes with room echo (it ignores the fading echo after each beep) and
with other sounds at other pitches. PC test (`tools/test_beep_detector.py`: synthetic alarms with
+/-10 % timing and 0.1-0.7 s room echo, mixed into real background audio):

| | detected within 30 s |
|---|---|
| smoke, SNR -10 / -5 / 0 / +5 / +10 / +20 dB | 80 / 92 / 100 / 100 / 100 / 100 % |
| CO, same SNRs | 84 / 76 / 96 / 84 / 84 / 80 % |
| **false alerts** on 16.3 h of everyday sound (6800 recordings) | **0** |

A real alarm keeps sounding, so one missed cycle just means the alert comes a few seconds later.
Not covered: 520 Hz low-frequency alarms and non-standard beepers (the AI "alarm" class still
listens for those). Check yours: record it with the watch (label ALARM) and run
`tools/test_beep_detector.py --wav <file>`, or watch `-ai log on` while it sounds.

## Using it

* **Sound page** (swipe to it, between Voice and Lights): bell button turns listening on/off and
  shows the last alert. Listening starts automatically at boot **if the self-test passes**.
* An alert shows e.g. `DOOR KNOCK 87 %` and vibrates (5 long buzzes for alarm/siren, 3 for
  knock/doorbell, even if button haptics are off). Tap the popup to stop.
* Same sound won't alert again within 8 s.

## Serial console (USB, e.g. `picocom /dev/ttyACM0`)

| command | |
|---|---|
| `-ai test` | runs the built-in test clip; **PASS** = the watch computes the same as the PC |
| `-ai` | status: runs, quiet windows skipped, alerts, inference time, dropped samples, beep-detector counts |
| `-ai on` / `-ai off` | start / stop listening |
| `-ai log on` | print every AI result while there is sound, and every beep / cycle the smoke-CO detector sees |

`mic dropped` > 0 means the main loop was too slow to keep up (something blocked for > 1 s).

## Recording training data with the watch

1. Voice page → tap the button at the top until it shows what you'll record:
   `ALARM`, `KNOCK`, `BELL`, `SIREN`, `BG` (background = everything that must NOT alert).
2. Record. Files are saved as `REC/KNOCK_261108_113000.WAV`.
3. Only record sounds you own or that are openly licensed. Don't record copyrighted videos.
4. Plug in USB and on the PC:
   ```bash
   cd training
   python import_watch_recordings.py /run/media/$USER/<watch>/REC
   python prepare_open_dataset.py
   python train.py
   cd .. && tools/update_model.sh
   cmake --build build/Debug     # then flash and run -ai test
   ```

## Updating the model

ST's runtime and generated network code are proprietary (SLA0044), so they aren't in this repo.
Install ST Edge AI Core (free from st.com; on Arch or other non-Debian systems use
`stedgeai-linux-*.zip` from inside the `x-cube-ai-linux-*.zip` download, unpacked to
`~/ST/stedgeai/10.2.0`, or set `STEDGEAI_ROOT`), then run `tools/update_model.sh` once after cloning
and again after each retrain. It creates or copies:

* `Core/AI/network*.c/.h`: generated network
* `Core/AI/model_config.h`, `model_tables.c`, `test_vector.h`: mel filters, normalisation, class
  names and self-test clip from `train.py` (they **must** come from the same training run)
* `Middlewares/ST/AI/`: runtime library + headers (with one over-aligned section fixed, which
  otherwise wastes 64 KB of flash)

Adding a class (a new folder in `training/data/raw/`) needs no firmware change except a
friendly name in `sa_pretty()` (ui_watch.c) and, if you want to record it on the watch, a label
in `recorder.c` + `training/import_watch_recordings.py`.

## Resources

| | RAM | Flash |
|---|---|---|
| network activations | 22 KB | 31 KB weights |
| ST runtime | – | ~31 KB |
| features, spectrogram, buffers | ~25 KB | ~4 KB tables |
| self-test clip | – | ~37 KB (remove `test_vector.h` include + `SD_SelfTest` to save it) |

The mic stays on while listening (more battery than with alerts off). Tuning is at the top of
`sound_detector.c`: `DETECT_THRESHOLD`, `SMOOTH_N`, `COOLDOWN_MS`, `ENERGY_GATE`.

This is an extra aid, not a certified alerting device: keep a proper strobe / bed-shaker smoke
alarm and test the watch against your real alarm, doorbell and doors.
