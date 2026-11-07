/** @file weather.h  Barometric weather prediction (pressure tendency).
 *
 *  Pressure is sampled every 10 minutes into a 24 h history (saved to EEPROM
 *  every 30 min, so reboots keep it). The forecast comes from the change over
 *  the last 3 hours, using the standard meteorological thresholds:
 *
 *     3 h change (hPa)   forecast
 *     <= -6.0            Storm likely      (falling very rapidly)
 *     <= -3.5            Rain likely       (falling quickly)
 *     <= -1.5            Getting worse
 *     <  +1.5            Steady
 *     <  +3.5            Improving
 *     >= +3.5            Clearing, windy   (rising quickly)
 *
 *  Needs ~1 h of history before the first forecast. A jump of more than
 *  3 hPa within 10 minutes means you changed height (lift, car, hill), not
 *  the weather, so the trend restarts instead of giving a false alarm. */
#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool        valid;          /* false until ~1 h of data       */
    int16_t     trend_x10;      /* hPa change per 3 h, x10        */
    int8_t      arrow;          /* -1 falling, 0 steady, +1 rising */
    const char *text;           /* "Steady", "Rain likely", ...   */
    uint16_t    hours_x10;      /* history length, hours x10      */
} weather_t;

void Weather_Init(void);        /* after Sensors_Init() (needs EEPROM + RTC) */
void Weather_Task(void);        /* main loop                                 */
const weather_t *Weather_Get(void);
void Weather_PrintReport(void); /* "-wx" serial command                      */

#endif
