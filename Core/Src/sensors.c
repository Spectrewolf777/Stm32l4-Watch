#include "sensors.h"
#include "main.h"
#include "max17048.h"
#include "hdc2010.h"
#include "bmp388.h"
#include "lis3mdl.h"
#include "drv2605l.h"
#include "m24512.h"
#include "lsm6dsv16x_stepcounter.h"
#include "rtc_tasks.h"
#include "power_mon.h"
#include <stdio.h>
#include <stdlib.h>

/* Poll periods (ms): [screen on, screen off] */
#define BAT_PERIOD_ON    5000U
#define BAT_PERIOD_OFF  60000U
#define ENV_PERIOD_ON    2000U
#define ENV_PERIOD_OFF  60000U
#define SLOW_COMPASS_MS  1000U
#define FAST_COMPASS_MS    50U
#define SERVICE_MS         50U
#define HDC_CONVERT_MS     10U

static sensors_data_t s_data;
static bool     s_fast_compass;
static bool     s_hdc_pending;
static uint32_t t_bat, t_env, t_hdc, t_compass, t_service;
static bool     s_first = true;

/* Is a cell actually fitted? With no battery the MAX17048 just measures the
 * charger output, which gives a fake voltage / percentage.
 *  - running from battery (not USB): a battery obviously exists
 *  - on USB: a real cell holds a steady voltage between 3.0 and 4.25 V; the
 *    charger output with no cell jumps around or sits outside that range
 *  - "-set battery 1/2" overrides the guess (none / installed) */
#define BAT_HIST 8
static void update_battery_presence(uint16_t mv)
{
    static uint16_t hist[BAT_HIST];
    static uint8_t  n, idx;
    uint8_t mode = Settings_Get()->battery_mode;

    hist[idx] = mv; idx = (idx + 1) % BAT_HIST;
    if (n < BAT_HIST) n++;

    if (mode == 1) { s_data.bat_present = false; return; }
    if (mode == 2) { s_data.bat_present = true;  return; }
    if (!Power_OnUsb()) { s_data.bat_present = true; return; }

    uint16_t lo = 0xFFFF, hi = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (hist[i] < lo) lo = hist[i];
        if (hist[i] > hi) hi = hist[i];
    }
    s_data.bat_present = (n >= 4) && (hi - lo <= 60) && lo >= 3000 && hi <= 4250;
}

void Sensors_Init(void)
{
    /* EEPROM first so settings (compass cal, sea level) are ready */
    M24512_Init();
    Settings_Load();

    s_data.bat_ok  = MAX17048_Init();
    s_data.env_ok  = HDC2010_Init();
    s_data.baro_ok = BMP388_Init();
    s_data.mag_ok  = LIS3MDL_Init();
    DRV2605L_Init();

    BMP388_SetSeaLevel(Settings_Get()->sea_level_pa);
    s_data.heading_deg = -1;
}

const sensors_data_t *Sensors_Get(void) { return &s_data; }
void Sensors_SetFastCompass(bool on) { s_fast_compass = on; }

static bool due(uint32_t *last, uint32_t period)
{
    uint32_t now = HAL_GetTick();
    if (s_first || now - *last >= period) { *last = now; return true; }
    return false;
}

void Sensors_Task(void)
{
    bool on = Display_IsOn();

    /* Gyro fusion only while the compass page is visible (gyro costs power) */
    Compass_FusionEnable(s_data.mag_ok && on && s_fast_compass);
    Compass_FusionTask();

    if (due(&t_service, SERVICE_MS)) {
        DRV2605L_Service();
        Compass_Service();
    }

    /* On USB poll every second so a missing battery is spotted quickly */
    uint32_t bat_period = Power_OnUsb() ? 1000U : (on ? BAT_PERIOD_ON : BAT_PERIOD_OFF);
    if (s_data.bat_ok && due(&t_bat, bat_period)) {
        max17048_data_t b;
        if (MAX17048_Read(&b)) {
            s_data.bat_mv       = b.mv;
            s_data.bat_percent  = b.percent;
            s_data.bat_charging = b.charging;
            update_battery_presence(b.mv);
        }
    }

    if (due(&t_env, on ? ENV_PERIOD_ON : ENV_PERIOD_OFF)) {
        if (s_data.env_ok && HDC2010_Trigger()) {
            s_hdc_pending = true;
            t_hdc = HAL_GetTick();
        }
        if (s_data.baro_ok) {
            uint32_t p; int16_t t;
            if (BMP388_Read(&p, &t)) {
                s_data.pressure_pa = p;
                s_data.altitude_m  = BMP388_AltitudeM(p);
                if (!s_data.env_ok) s_data.temp_x10 = t;  /* fallback temp */
            }
        }
    }

    if (s_hdc_pending && HAL_GetTick() - t_hdc >= HDC_CONVERT_MS) {
        int16_t t; uint16_t rh;
        s_hdc_pending = false;
        if (HDC2010_Read(&t, &rh)) {
            s_data.temp_x10     = t;
            s_data.humidity_x10 = rh;
        }
    }

    /* Compass failed to start at boot: retry every 5 s while the compass
     * page is showing (a failed attempt blocks for ~1 s, so not elsewhere) */
    static uint32_t t_mag_retry;
    if (!s_data.mag_ok && on && s_fast_compass && HAL_GetTick() - t_mag_retry >= 5000U) {
        t_mag_retry = HAL_GetTick();
        s_data.mag_ok = LIS3MDL_Init();
    }

    if (s_data.mag_ok && on &&
        due(&t_compass, s_fast_compass ? FAST_COMPASS_MS : SLOW_COMPASS_MS)) {
        s_data.heading_deg = Compass_Heading();
    }

    s_data.steps = StepCounter_GetSteps();
    s_first = false;
}

void Haptic(uint8_t effect)
{
    if (Settings_Get()->haptics_on) DRV2605L_Play(effect);
}

/* Prints  "-12.3" style fixed point without needing printf float support */
static void fmt_x10(char *buf, int n, int32_t v)
{
    snprintf(buf, n, "%s%ld.%ld", v < 0 ? "-" : "", labs(v) / 10, labs(v) % 10);
}

void Sensors_PrintReport(void)
{
    char t[12], h[12];
    fmt_x10(t, sizeof t, s_data.temp_x10);
    fmt_x10(h, sizeof h, s_data.humidity_x10);

    printf("\r\n--- Sensors ---\r\n");
    if (s_data.bat_present)
        printf("Battery : %u%%  %u mV  %s\r\n", s_data.bat_percent, s_data.bat_mv,
               s_data.bat_charging ? "charging" : "");
    else
        printf("Battery : none detected (gauge reads %u mV from the charger)\r\n", s_data.bat_mv);
    printf("Temp    : %s C   Humidity: %s %%\r\n", t, h);
    printf("Pressure: %lu Pa  Altitude: %ld m\r\n",
           (unsigned long)s_data.pressure_pa, (long)s_data.altitude_m);
    printf("Heading : %d deg%s\r\n", s_data.heading_deg,
           Compass_CalRunning() ? " (calibrating)" : "");
    printf("Steps   : %u\r\n", s_data.steps);
    fflush(stdout);
}

void Sensors_PrintStatus(void)
{
    printf("\r\n--- Devices ---\r\n");
    printf("MAX17048 fuel gauge : %s\r\n", MAX17048_Present() ? "OK" : "not found");
    printf("HDC2010  humidity   : %s\r\n", HDC2010_Present()  ? "OK" : "not found");
    printf("BMP388   barometer  : %s\r\n", BMP388_Present()   ? "OK" : "not found");
    printf("LIS3MDL  compass    : %s\r\n", LIS3MDL_Info());
    printf("DRV2605L haptics    : %s\r\n", DRV2605L_Present() ? "OK" : "not found (EN pin low?)");
    if (M24512_Present())
        printf("M24512   EEPROM     : OK at 0x%02X\r\n", M24512_Address());
    else
        printf("M24512   EEPROM     : not found\r\n");
    fflush(stdout);
}
