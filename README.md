# Open-Source Sound Alert Smartwatch

An open-source smartwatch that helps deaf and hard-of-hearing people notice important sounds. It listens with its own microphone and alerts the wearer with vibration and on-screen text for **smoke and carbon monoxide alarms, door knocks, doorbells and sirens**.

All audio is processed on the watch, with no phone or internet connection needed.

Built around the **STM32L4P5** and **nRF52805**, with an **LVGL** touch UI on a round 240×240 display and **TinyUSB** over USB-C. The hardware design, firmware, training code and 3D case are open; only ST's AI runtime is proprietary and is downloaded separately.

> **Safety note:** this is an assistive prototype, not a certified life-safety device. Use it alongside certified strobe or bed-shaker alarms, not instead of them.

<img width="2000" height="1600" alt="Smartwatch" src="https://github.com/user-attachments/assets/88df3cd3-ec02-4325-b380-abda1396d602" />

## Links

| | |
|---|---|
| Sound alerts demo | https://www.youtube.com/shorts/HwIivaiX0Go|
| General demo | https://www.youtube.com/watch?v=jnEd2pBWlZo |
| BLE demo and setup | https://www.youtube.com/watch?v=e0hORUg1qQ8 |
| PCB and hardware files | https://oshwlab.com/moin0192837465/project_glmgovsw |
| nRF52 BLE firmware | https://github.com/Spectrewolf777/Nrf52-code-for-l4-watch |
| Sound alerts documentation | [docs/SOUND_ALERTS.md](docs/SOUND_ALERTS.md) |
| Model training code | [training/](training/) |

## How sound alerts work

Two detectors run side by side on the STM32:

| Detector | Listens for | How it works |
|---|---|---|
| **Neural network** | Alarms, door knocks, doorbells, sirens | Small int8 CNN on log-mel audio features |
| **Beep-pattern rules** | Standard smoke (T3) and CO (T4) alarm patterns | Tracks a steady ~3 kHz tone and its beep rhythm; no training needed |


**When a sound is detected:**
- A popup such as `DOOR KNOCK` or `CARBON MONOXIDE ALARM` wakes the screen.
- The watch vibrates even if button haptics are off, with longer vibration for alarms and sirens.
- The Sound page shows the last alert and turns listening on or off.

**Model:** about 32k parameters, 3.0M MACC, 31 KB weights and 22 KB activation RAM, deployed with ST Edge AI Core.

**Training data:** FSD50K clips licensed CC0 or CC-BY, plus synthetic alarm sounds. Every clip is credited in [`training/ATTRIBUTION_FSD50K.csv`](training/ATTRIBUTION_FSD50K.csv).

**Improving it yourself:** the Voice page can label recordings (ALARM, KNOCK, BELL, SIREN, BG), so new training data can be captured through the watch's own microphone.

## Current results

An earlier version of the neural network detected a carbon monoxide alarm on the real watch, both up close and through a wall.

**Neural network, held-out real recordings, per ~1 s window:**

| Class | Correct |
|---|---|
| Background (no alert) | 92.0% |
| Doorbell | 78.7% |
| Door knock | 67.9% |
| Siren | 54.7% |
| Alarm | 42.5% |
| **Overall** | **85.0%** |

These are per-window figures. A sound lasting several seconds gets repeated chances to be caught. On 1.6 hours of dense, varied background audio, simulated firmware logic gave about 14 false alerts per hour at a 0.9 threshold and 27 per hour at 0.8. The firmware uses 0.85.

**Beep-pattern detector, PC simulation** (synthetic alarms with timing variation and room echo, mixed into real background audio at -10 to +20 dB SNR):

| Measure | Result |
|---|---|
| Smoke alarm detected within 30 s | 80–100% (median 7.6 s) |
| CO alarm detected within 30 s | 76–96% (median 13.5 s) |
| False alerts in 16.3 h of background audio | 0 |

**Known limits:**
- The beep detector does not cover 520 Hz low-frequency alarms or non-standard beepers.
- The neural network still needs recordings made through the watch to reduce false alerts.
- Battery life while listening has not been measured yet.

## Features

**Sound alerts**
- On-device detection of alarms, knocks, doorbells and sirens.
- Self-test at boot that compares the watch's results with values computed on a PC.
- The recorder and detectors can share the microphone at the same time.

**Touch UI**
- Swipe between **Watchface → Sensors → Compass → Stopwatch → Timer → Voice → Sound → Lights**. Swipe down returns to the watchface.
- Touch to wake, returning to the page you were on.

**Watch functions**
- Time, day and date from the RV-3028 RTC.
- Step count, battery percentage and Bluetooth status.
- Stopwatch and countdown timer with vibration alarm.
- Double-tap the watchface to start or stop a voice recording.

**Sensors**
- Temperature, humidity, pressure and altitude.
- Barometer weather forecast with a storm alert.
- Tilt-compensated compass using gyroscope, accelerometer and magnetometer fusion, with on-watch calibration.

**Voice recorder**
- 16 kHz WAV files saved to the onboard 512 MB flash.
- Recordings appear on your PC over USB-C.

**Connectivity**
- USB-C serial console and mass storage.
- Nordic UART bridge over BLE for setting the time, notifications and weather text.
- Lights page: controls a Tapo smart bulb through BLE adverts and a small Python script, with no pairing.

## Hardware

| Component | Part Number | Function |
| :--- | :--- | :--- |
| **MCU** | `STM32L4P5CGU6` | Ultra-low-power Cortex-M4F; runs the sound detectors |
| **Microphone** | `IM68D128BV01XTMA1` | Digital PDM microphone |
| **Haptic Driver** | `DRV2605LYZFR` | Vibration alerts |
| **Wireless** | `NRF52805-CAAA-R` | Bluetooth Low Energy 5.2 SoC |
| **Display** | `GC9A01` 1.28" 240×240 | Round IPS LCD over SPI |
| **Touch** | `CST816T` | Capacitive touch controller |
| **Storage** | `XTSDG04GWSIGA` | 4 Gbit (512 MB) SD NAND over SDMMC, FAT32 |
| **Charger** | `BQ25101YFPR` | Single-cell Li-ion/Li-Po charger |
| **Power Path** | `LM66200` | Dual ideal diode for battery/USB switching |
| **Battery Protection** | `XB5350D0` | Lithium battery protection IC |
| **Fuel Gauge** | `MAX17048G+T10` | Battery percentage and low-battery alert |
| **IMU** | `LSM6DSV16XTR` | 6-axis IMU, pedometer and sensor hub |
| **Magnetometer** | `LIS3MDLTR` | Compass, connected through the IMU sensor hub |
| **Accelerometer** | `BMA400` | Fitted but unused in this revision (wiring error) |
| **EEPROM** | `M24512-DFMC6TG` | Settings and weather history |
| **RTC** | `RV-3028-C7` | Low-power, high-accuracy real-time clock |
| **Humidity / Temp** | `HDC2010YPAR` | Humidity and temperature sensor |
| **Barometer** | `BMP388` | Altitude and weather trend |

**Display:** https://www.aliexpress.com/item/1005007293053050.html

<img width="3000" height="4000" alt="Watch hardware" src="https://github.com/user-attachments/assets/4e4e009c-f669-4896-b754-4356d55145fa" />
<img width="3000" height="4000" alt="Watch assembly" src="https://github.com/user-attachments/assets/9bec51a4-f078-400a-b80d-fcb8743b5c8c" />

**CNC aluminium case**

<img width="1894" height="3264" alt="CNC aluminium case" src="https://github.com/user-attachments/assets/b4b6c6af-3ee1-41a1-8c96-507016830dd6" />

## Build the firmware

Requires `arm-none-eabi-gcc`, `cmake`, `ninja` and ST Edge AI Core (free from st.com).

ST's AI runtime is proprietary, so it isn't in this repo. Unpack ST Edge AI Core to `~/ST/stedgeai/10.2.0`, or set `STEDGEAI_ROOT`, then run on Linux:

```bash
git clone https://github.com/Spectrewolf777/Stm32l4-Watch.git
cd Stm32l4-Watch
tools/update_model.sh
cmake --preset Debug
cmake --build build/Debug
```

The firmware is written to `build/Debug/WatchWatchWatch.elf`. Debug build usage is about 81% of RAM and 85% of flash.

## Programming the watch

**Method 1 (recommended): SWD programmer**
- Flashes both the **STM32** and **nRF52**, including BLE.
- Works with an **ST-Link** or **J-Link**. The **J-Link EDU Mini** is recommended because it supports both chips.
- Connect with a Tag-Connect cable such as the **TC2030-IDC-NL**. An adapter board may be needed.

**Method 2: USB-C DFU bootloader**
- Flash an `.elf` over USB-C with **STM32CubeProgrammer**.
- This cannot program the nRF52, so BLE won't work out of the box.
- If you aren't using BLE, you can disconnect its circuit by removing the **0 Ω resistor** in the BLE section.

**First-time setup**
1. Format the flash with `-format yes`, or as FAT32 (MBR) on your PC.
2. Calibrate the compass on the Compass page, then tap Set North.
3. Set the time with `-t` and `-d`, or from your phone over BLE.
4. Send `-ai test` and check that it reports PASS.

## USB serial commands

Open `/dev/ttyACM0` on Linux or the COM port on Windows. Type `-h` for the full list.

| Command | What it does |
| :--- | :--- |
| `-ai test` | Run the sound-detector self-test |
| `-ai on` / `-ai off` | Start or stop listening |
| `-ai log on` / `-ai log off` | Print live detector results |
| `-ai` | Sound-detector status |
| `-t HH:MM:SS` / `-d DD/MM/YY W` | Set time / date |
| `-s` | All sensor readings |
| `-dev` | Which chips were detected |
| `-rec start` / `-rec stop` | Voice recorder |
| `-wx` | Barometer forecast and pressure history |
| `-cal start` / `-cal stop`, `-mag` | Compass calibration and debug |
| `-n` / `-w` | Show a notification / weather text |
| `-buzz [effect]` | Test the vibration motor |
| `-pwr` | Battery and USB power status |
| `-set timeout/haptics/battery/sea ...` | Settings saved to EEPROM |
| `-sd`, `-format yes` | Check the flash / format it as FAT32 |

## Roadmap

**Software**
- Train on more recordings made through the watch's microphone.
- Use the accelerometer to reject false knock alerts from the wearer's own hand movements.
- Low-power sleep and measured battery life, including while listening.
- Forward sound alerts to a phone or smart lights over BLE.
- Phone app integration (e.g. Gadgetbridge), Home Assistant control and over-the-air updates.
- USB HID: keyboard, mouse and presentation clicker.

**Next PCB revision**
- Fix the BMA400 wiring and improve the microphone's sound port.
- Move to an **STM32U5** for better low-power states, possibly with a 1.8 V rail.
- USB High-Speed, a stereo microphone and larger storage.
- **nRF53** for BLE audio, an AMOLED screen, a light sensor and XIP flash for the UI.
- GPS for location, activity tracking and time sync.
- Coin cell RTC backup and a better battery position.
- Reduce the PCB diameter from 44 mm to 40 mm.

## Licence

Code: MIT. Hardware design files: CERN-OHL-P-2.0. Third-party components and training-data credits are listed in [THIRD_PARTY.md](THIRD_PARTY.md).
