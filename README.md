
## Demo Video
https://www.youtube.com/watch?v=jnEd2pBWlZo 

## BLE Demo and Setup Video
https://www.youtube.com/watch?v=e0hORUg1qQ8

## BLE Nrf52 code
https://github.com/Spectrewolf777/Nrf52-code-for-l4-watch

## Hardware Components

| Component | Part Number | Description / Function |
| :--- | :--- | :--- |
| **MCU** | `STM32L4P5CGU6` | Ultra-low-power ARM Cortex-M4 Microcontroller |
| **Wireless** | `NRF52805-CAAA-R` | Bluetooth Low Energy (BLE) 5.2 SoC |
| **PMIC / Charger** | `BQ25101YFPR` | Single-cell Li-Ion/Li-Pol battery charger |
| **Battery Protection** | `XB5350D0` | Integrated Lithium battery protection IC |
| **Fuel Gauge** | `MAX17048G+T10` | Ultra-low power ModelGauge Li+ battery fuel gauge |
| **Accelerometer** | `BMA400` | Ultra-low-power 3-axis accelerometer (pedometer/activity) |
| **IMU** | `LSM6DSV16XTR` | 6-axis IMU with embedded Machine Learning Core |
| **Magnetometer** | `LIS3MDLTR` | 3-axis digital magnetic sensor (compass) |
| **EEPROM** | `M24512-DFMC6TG` | 512-Kbit serial I2C EEPROM |
| **RTC** | `RV-3028-C7` | Ultra-clean, 1ppm high-stability real-time clock module |
| **Haptic Driver** | `DRV2605LYZFR` | ERM/LRA haptic driver with built-in library |
| **Humidity / Temp** | `HDC2010YPAR` | Low-power digital humidity and temperature sensor |
| **Barometer** | `BMP388` | High-performance digital barometric pressure sensor |
| **NAND Flash** | `XTSDG04GWSIGA` | Onboard high-density SPI flash storage |
| **Microphone** | `IM68D128BV01XTMA1` | High-performance digital PDM microphone |

# Hardware Open Source Design Location
https://oshwlab.com/moin0192837465/project_glmgovsw <------- Can be found here it live

# Display 
https://www.aliexpress.com/item/1005007293053050.html

## Project function
- Watch is fully configurable through usb c, can act as a keyboard, mouse , badusb and can communicate through commands.
- Can store audio recordings and activate audio recording with gestures.
- Track steps
- Time tracking and stopwatch function
- Link phone and watch through bluetooth to get other data link weather or whatever you want.
- Watch can be used in future to control custom project you make later as it has bluetooth and is open source and can be programmed later

## Application Scenarios
- **Hardware Hackers & Developers:** Because the watch acts as a BadUSB, keyboard, and mouse, it can be used for rapid payload deployment, IT security testing, or simply as a wearable presentation clicker.
- **Custom Home Automation:** Utilizing the nRF52 BLE chip, makers can pair the watch to Home Assistant or custom ESP32 nodes to control smart home devices directly from their wrist.
- **Everyday Fitness & Utilities:** With the onboard BMA400 and RTC, it serves as an everyday pedometer, stopwatch, and voice-recorder (saving directly to the NAND flash).



## Current Software Implementation

- **Dual USB Interface:** Configured via TinyUSB to expose both **CDC (Virtual COM Port)** for serial debugging which can accept commands and **Mass Storage Class (MSC)** for direct file access.
- **Storage Integration:** Connection to the onboard **Storage**, allowing the device to act as a standard USB flash drive when plugged into a host.
- **RTC** can set time and recieve time updating ui.
- **Motion & Orientation Tracking** - IMU to reconize when screen tilted and to count steps.


## Planned Software Features

- **RTC:** Need to setup timers to count down on ui.
- **Wireless Connectivity:** Integrate a **Bluetooth** stack to enable smartphone data syncing.
- Implement a digital **compass** for accurate heading and orientation sensing.
- **Motion & Orientation Tracking:** - adding more gestures to control microphone.
- act as a keyboard, mouse , badusb and can communicate through commands.
- link phone and watch through bluetooth to get other data link weather or whatever you want.
- Watch can be used in future to control custom project you make later as it has bluetooth and is open source and can be programmed later 
- store audio recordings and activate audio recording with gestures.

<img width="3000" height="4000" alt="nnnn" src="https://github.com/user-attachments/assets/4e4e009c-f669-4896-b754-4356d55145fa" />
Battery not soldered yet that why i am using usb as coding hasnt finished and wanted to do more current testing.

<img width="2000" height="1600" alt="Untitled design" src="https://github.com/user-attachments/assets/88df3cd3-ec02-4325-b380-abda1396d602" />
<img width="3000" height="4000" alt="20260705_174336" src="https://github.com/user-attachments/assets/9bec51a4-f078-400a-b80d-fcb8743b5c8c" />



## Programming Instructions

This board can be programmed using one of two methods depending on your hardware setup and whether you need Bluetooth Low Energy (BLE) functionality.

- **Method 1 (Recommended): SWD Programmer**
  - Allows you to flash both the **STM32** and the **nRF52** microcontrollers for full functionality (including BLE).
  - Works with an **ST-Link** or **J-Link** (the **J-Link EDU Mini** is highly recommended as it natively supports both ecosystems).
  - Connection can be made via a Tag-Connect cable like the **TC2030-IDC-NL** (an adapter board may be required depending on your toolchain).

- **Method 2: USB-C DFU Bootloader**
  - Allows direct flashing over USB-C using an `.elf` file and **STM32CubeProgrammer**.
  - **Limitation:** This method **cannot** program the nRF52 chip, meaning you will not have BLE functionality out of the box. 
  - **Hardware Note:** If you are omitting BLE, you can completely disconnect the BLE circuit by desoldering the **0 $\Omega$ resistor** located in the BLE section of the board.




## Things to improve on next pcb design

- **Upgraded Architecture:** switch from the STM32L4 to the more powerful **STM32U5** advanced low-power states and maybe buck down to 1.8V instead of 3.3V.
- **High-Speed USB:** Upgrade the USB interface to **USB High-Speed (HS)** for faster write and read speeds.
- **Stereo Audio:** Replace the current mono microphone setup with a **stereo microphone**.
- **Storage Expansion:** Upgrade the onboard flash storage to a **larger capacity** .
- **NRF53:** Allows ble headphones to connect in order to send audio from sd flash or microphones to headphones.
- **Screen:** AMOLED Screen.
- **Sensor:** Add light and any other missing sensors.
- **XIP:** More storage for ui.
- **Form Factor & Enclosure Optimization:** - Reduce the overall PCB diameter from **44mm down to 40mm**.
  - Relocate and fix the **battery positioning**.
