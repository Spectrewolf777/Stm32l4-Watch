/** @file m24512.h  M24512 64 KB I2C EEPROM + a small persistent settings block.
 *
 *  Address is 0x50..0x57 depending on the E0-E2 pins. 0x52 is skipped while
 *  probing because the RV-3028 RTC lives there. */
#ifndef M24512_H
#define M24512_H

#include <stdbool.h>
#include <stdint.h>

#define M24512_SIZE        65536U
#define M24512_PAGE_SIZE   128U

bool M24512_Init(void);
bool M24512_Present(void);
uint8_t M24512_Address(void);
bool M24512_Read(uint16_t mem_addr, uint8_t *buf, uint16_t len);
bool M24512_Write(uint16_t mem_addr, const uint8_t *buf, uint16_t len);  /* page-aware */

/* ---- Persistent watch settings (stored at EEPROM address 0) ---- */
typedef struct {
    int16_t  mag_offset[3];     /* compass hard-iron offsets (raw LSB) */
    int16_t  mag_scale[3];      /* soft-iron scale x1000               */
    uint32_t sea_level_pa;      /* barometer altitude reference        */
    uint16_t screen_timeout_s;  /* backlight timeout                   */
    uint8_t  haptics_on;
    /* v2: compass north alignment (from "Set North") */
    int8_t   compass_dir;       /* +1 / -1 turning direction, 0 = not aligned */
    int16_t  compass_north_x10; /* raw angle (0.1 deg) that means north   */
    uint8_t  compass_tilt_map;  /* 0 = no tilt comp, 1..8 = IMU->mag axis map
                                 * (fits in old padding: layout unchanged) */
    uint8_t  battery_mode;      /* 0 auto-detect, 1 no battery, 2 installed
                                 * (last padding byte: layout unchanged) */
} watch_settings_t;

/* Loads settings (defaults if EEPROM missing/blank/corrupt). */
void Settings_Load(void);
bool Settings_Save(void);
watch_settings_t *Settings_Get(void);

#endif
