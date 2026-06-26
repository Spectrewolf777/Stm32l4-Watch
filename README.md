

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


## Current Software Implementation

- **Dual USB Interface:** Configured via TinyUSB to expose both **CDC (Virtual COM Port)** for serial debugging and **Mass Storage Class (MSC)** for direct file access.
- **Storage Integration:** Connection to the onboard **Storage**, allowing the device to act as a standard USB flash drive when plugged into a host.


## Planned Software Features

- **Advanced Timing Management:** Implement **RTC software timers** to handle scheduling, sensor reads, and low power wake up cycles.
- **Wireless Connectivity:** Integrate a **Bluetooth** stack to enable smartphone data syncing.
- **Motion & Orientation Tracking:** - Integrate an **IMU** for precise motion tracking and gesture recognition.
  - Implement a digital **compass** for accurate heading and orientation sensing.
- **Fitness Tracking:** Develop a dedicated **step-counting (pedometer)**.


 
<img width="554" height="734" alt="watch" src="https://github.com/user-attachments/assets/f2dee0e2-ef63-4122-9adb-22f92b1a247c" />
<img width="950" height="990" alt="Capt3ure" src="https://github.com/user-attachments/assets/5668f43c-fb72-4d55-9978-51397f0ff26f" />
<img width="936" height="748" alt="Capture2" src="https://github.com/user-attachments/assets/962126b6-9c2e-4f1a-8837-a52186947bc2" />


## Things to improve on next pcb design

- **Upgraded Architecture:** switch from the STM32L4 to the more powerful **STM32U5** advanced low-power states.
- **High-Speed USB:** Upgrade the USB interface to **USB High-Speed (HS)** for faster write and read speeds.
- **Stereo Audio:** Replace the current mono microphone setup with a **stereo microphone**.
- **Real-Time Audio Monitoring:** adding a **3.5mm audio jack** to allow direct, real-time monitoring of signal-processed audio.
- **Storage Expansion:** Upgrade the onboard flash storage to a **larger capacity** .
- **Form Factor & Enclosure Optimization:** - Reduce the overall PCB diameter from **44mm down to 40mm**.
  - Relocate and fix the **battery positioning**.
