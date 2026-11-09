# Sound-alert model training

Trains the 5-class model the watch runs: background, alarm, door_knock, doorbell, siren. It uses a
small int8 depthwise-separable CNN on 40-band log-mel features (16 kHz, about 1 s per window).

Only openly licensed data is used: FSD50K clips marked CC0 or CC-BY, plus synthetic alarms and your
own recordings. Non-commercial and Sampling+ clips are filtered out per clip by
`prepare_open_dataset.py`, and every clip used is credited in `ATTRIBUTION_FSD50K.csv`.

A trained model is already in `out_open/`, so you only need this folder to retrain.

## Setup (needs Python 3.11 for TensorFlow 2.15)

```bash
cd training
uv venv -p 3.11 .venv && source .venv/bin/activate
uv pip install -r requirements.txt
```

## Get the data (about 24.5 GB from Zenodo record 4060432)

Download these into `data/downloads/fsd50k/`:

* `FSD50K.ground_truth.zip`: unzip it there
* `FSD50K.metadata.zip`: unzip it into `data/downloads/fsd50k/meta/`
* `FSD50K.dev_audio.z01` to `.z05` and `.zip`, `FSD50K.eval_audio.z01` and `.zip`

## Train

```bash
python prepare_fsd50k.py         # merges the archives, sorts clips into data/raw/ (needs `zip`)
python synth_alarms.py           # synthetic smoke/CO alarms and sirens
python prepare_open_dataset.py   # builds data/raw_open/ and ATTRIBUTION_FSD50K.csv
python train.py                  # writes out_open/ and prints test results
cd .. && tools/update_model.sh   # generates the network code for the firmware
```

`train.py` reports accuracy on held-out real recordings and estimates false buzzes per hour by
playing held-out background audio through the same decision logic as the firmware.

## Add recordings made with the watch

These usually help more than anything else, because the model learns how sounds come through the
watch's own mic and case. See `docs/SOUND_ALERTS.md`. Only record sounds you own or that are openly
licensed, then run `python import_watch_recordings.py <watch>/REC` before `prepare_open_dataset.py`.
