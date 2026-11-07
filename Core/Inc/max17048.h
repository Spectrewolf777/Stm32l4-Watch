/** @file max17048.h  MAX17048 Li-ion fuel gauge (ModelGauge, no sense resistor). */
#ifndef MAX17048_H
#define MAX17048_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t mv;          /* cell voltage, millivolts                     */
    uint8_t  percent;     /* state of charge 0..100                       */
    int16_t  rate_x10;    /* charge rate in 0.1 %/hour (+ = charging)     */
    bool     charging;    /* derived from rate (BQ25101 has no I2C)       */
} max17048_data_t;

#define MAX17048_LOW_BAT_PERCENT  10   /* ALRT pin fires below this */

/* Alert flags returned by MAX17048_ServiceAlert() */
#define MAX17048_ALERT_RESET     0x01   /* RI: gauge was reset            */
#define MAX17048_ALERT_VHIGH     0x02
#define MAX17048_ALERT_VLOW      0x04
#define MAX17048_ALERT_VRESET    0x08
#define MAX17048_ALERT_SOC_LOW   0x10   /* HD: below LOW_BAT_PERCENT      */
#define MAX17048_ALERT_SOC_CHG   0x20   /* SC: 1% change (if ALSC set)    */

bool MAX17048_Init(void);
/* Reads + clears the alert flags and releases the ALRT pin. */
uint8_t MAX17048_ServiceAlert(void);
bool MAX17048_Read(max17048_data_t *out);
bool MAX17048_Present(void);
uint8_t MAX17048_Version(void);

#endif
