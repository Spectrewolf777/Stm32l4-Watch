/** @file sensors.h  Starts every I2C peripheral and polls them on a schedule.
 *  (BMA400 intentionally not used.) */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* battery - MAX17048 */
    bool     bat_ok;
    uint16_t bat_mv;
    uint8_t  bat_percent;
    bool     bat_charging;
    bool     bat_present;    /* false = no cell fitted (gauge reads charger) */
    /* environment - HDC2010 */
    bool     env_ok;
    int16_t  temp_x10;       /* 0.1 degC */
    uint16_t humidity_x10;   /* 0.1 %RH  */
    /* barometer - BMP388 */
    bool     baro_ok;
    uint32_t pressure_pa;
    int32_t  altitude_m;
    /* compass - LIS3MDL */
    bool     mag_ok;
    int16_t  heading_deg;    /* -1 = invalid */
    /* steps - LSM6DSV16X */
    uint16_t steps;
} sensors_data_t;

void Sensors_Init(void);
void Sensors_Task(void);                 /* call from main loop */
const sensors_data_t *Sensors_Get(void);
void Sensors_SetFastCompass(bool on);    /* 5 Hz heading while compass page shown */
void Sensors_PrintReport(void);          /* human readable, over USB */
void Sensors_PrintStatus(void);          /* which chips were found */

/* Haptic feedback helper; respects the haptics_on setting */
void Haptic(uint8_t effect);

#endif
