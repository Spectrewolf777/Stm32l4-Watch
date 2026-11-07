#include "weather.h"
#include "sensors.h"
#include "m24512.h"
#include "rv_3028_c7.h"
#include "ui_watch.h"
#include "drv2605l.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define WX_PERIOD_MS     (10UL * 60UL * 1000UL)   /* one sample / 10 min    */
#define WX_SLOTS         144                       /* 24 h of samples        */
#define WX_3H            18                        /* samples in 3 hours     */
#define WX_MIN_SAMPLES   7                         /* 1 h before forecasting */
#define WX_SAVE_EVERY    3                         /* EEPROM save / 30 min   */
#define WX_JUMP_X10      30                        /* 3 hPa in 10 min = moved */
#define WX_ALERT_X10     (-40)                     /* -4 hPa / 3 h -> alert  */
#define WX_ALERT_GAP_MS  (6UL * 3600UL * 1000UL)   /* max one alert / 6 h    */

#define WX_EE_ADDR       0x0100                    /* settings live at 0x0000 */
#define WX_MAGIC         0x57580001UL

extern rv3028_handle_t rtc_handle;

/* Pressure stored as hPa x10 (e.g. 10132 = 1013.2 hPa), oldest..newest */
typedef struct {
    uint32_t magic;
    uint32_t last_min;          /* RTC minutes when the newest sample was taken */
    uint16_t count;
    uint16_t p[WX_SLOTS];
    uint16_t crc;
} wx_store_t;

static wx_store_t s_st;
static weather_t  s_wx = { .text = "collecting data" };
static uint32_t   s_next_ms;
static uint8_t    s_unsaved;
static uint32_t   s_last_alert_ms;
static bool       s_alerted_once;

static uint16_t crc16(const uint8_t *d, uint32_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*d++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* Minutes since 2000-01-01 from the RV-3028, for "how long was I off?" */
static bool rtc_minutes(uint32_t *out)
{
    static const uint16_t cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    rv3028_time_t t;
    if (rv3028_get_time(&rtc_handle, &t) != HAL_OK || t.month < 1 || t.month > 12) return false;

    uint32_t y    = t.year;
    uint32_t days = y * 365U + (y + 3U) / 4U + cum[t.month - 1] + (t.date - 1U);
    if (t.month > 2 && (y % 4U) == 0) days++;
    *out = (days * 24U + t.hours) * 60U + t.minutes;
    return true;
}

static void save(void)
{
    uint32_t now;
    if (rtc_minutes(&now)) s_st.last_min = now;
    s_st.magic = WX_MAGIC;
    s_st.crc   = crc16((const uint8_t *)&s_st, offsetof(wx_store_t, crc));
    M24512_Write(WX_EE_ADDR, (const uint8_t *)&s_st, sizeof(s_st));
    s_unsaved = 0;
}

static void evaluate(void)
{
    s_wx.hours_x10 = (uint16_t)(s_st.count > 1 ? (s_st.count - 1) * 10U / 6U : 0);

    if (s_st.count < WX_MIN_SAMPLES) {
        s_wx.valid = false;
        s_wx.arrow = 0;
        s_wx.text  = "collecting data";
        return;
    }

    /* Change over the last 3 h; with only 1-3 h of data, scale it up */
    uint16_t span = (s_st.count - 1 < WX_3H) ? (uint16_t)(s_st.count - 1) : WX_3H;
    int32_t  now  = s_st.p[s_st.count - 1];
    int32_t  old  = s_st.p[s_st.count - 1 - span];
    int32_t  tr   = (now - old) * WX_3H / span;

    s_wx.valid     = true;
    s_wx.trend_x10 = (int16_t)tr;
    s_wx.arrow     = tr <= -15 ? -1 : tr >= 15 ? 1 : 0;

    if      (tr <= -60) s_wx.text = "Storm likely";
    else if (tr <= -35) s_wx.text = "Rain likely";
    else if (tr <= -15) s_wx.text = "Getting worse";
    else if (tr <   15) s_wx.text = "Steady";
    else if (tr <   35) s_wx.text = "Improving";
    else                s_wx.text = "Clearing, windy";

    /* Rapid fall warning (needs 2 h of data so a short blip can't trigger it) */
    if (tr <= WX_ALERT_X10 && s_st.count >= 13 &&
        (!s_alerted_once || HAL_GetTick() - s_last_alert_ms >= WX_ALERT_GAP_MS)) {
        s_alerted_once  = true;
        s_last_alert_ms = HAL_GetTick();
        UI_ShowNotification(tr <= -60 ? "Pressure falling fast\nStorm likely" :
                                        "Pressure dropping\nRain likely");
    }
}

static void add_sample(uint16_t p_x10)
{
    /* Big jump in 10 min = changed height (lift, car, hill), not weather */
    if (s_st.count > 0 && abs((int)p_x10 - (int)s_st.p[s_st.count - 1]) > WX_JUMP_X10)
        s_st.count = 0;

    if (s_st.count == WX_SLOTS) {
        memmove(&s_st.p[0], &s_st.p[1], (WX_SLOTS - 1) * sizeof(s_st.p[0]));
        s_st.count--;
    }
    s_st.p[s_st.count++] = p_x10;

    if (++s_unsaved >= WX_SAVE_EVERY) save();
    evaluate();
}

void Weather_Init(void)
{
    uint32_t now;
    memset(&s_st, 0, sizeof(s_st));
    s_next_ms = HAL_GetTick() + 60000U;            /* first sample after 1 min */

    if (!M24512_Read(WX_EE_ADDR, (uint8_t *)&s_st, sizeof(s_st)) ||
        s_st.magic != WX_MAGIC || s_st.count > WX_SLOTS ||
        s_st.crc != crc16((const uint8_t *)&s_st, offsetof(wx_store_t, crc)) ||
        !rtc_minutes(&now)) {
        memset(&s_st, 0, sizeof(s_st));
        evaluate();
        return;
    }

    /* Keep the saved history only if we were off for less than ~1 sample
     * period beyond the last save (otherwise the 10-min spacing is broken) */
    uint32_t off_min = now - s_st.last_min;
    if (now < s_st.last_min || off_min > 40U) {
        s_st.count = 0;
    } else {
        uint32_t wait = (off_min >= 10U) ? 1U : (10U - off_min);
        s_next_ms = HAL_GetTick() + wait * 60000U;
    }
    evaluate();
}

void Weather_Task(void)
{
    const sensors_data_t *d = Sensors_Get();
    if ((int32_t)(HAL_GetTick() - s_next_ms) < 0) return;
    s_next_ms += WX_PERIOD_MS;

    if (!d->baro_ok || d->pressure_pa < 30000U || d->pressure_pa > 110000U) return;
    add_sample((uint16_t)(d->pressure_pa / 10U));
}

const weather_t *Weather_Get(void) { return &s_wx; }

void Weather_PrintReport(void)
{
    printf("\r\n--- Weather (barometer) ---\r\n");
    printf("History : %u samples (%u.%u h)\r\n", s_st.count, s_wx.hours_x10 / 10, s_wx.hours_x10 % 10);
    if (s_st.count)
        printf("Now     : %u.%u hPa\r\n", s_st.p[s_st.count - 1] / 10, s_st.p[s_st.count - 1] % 10);
    if (s_wx.valid)
        printf("Trend   : %s%d.%d hPa / 3 h\r\n", s_wx.trend_x10 < 0 ? "-" : "+",
               abs(s_wx.trend_x10) / 10, abs(s_wx.trend_x10) % 10);
    printf("Forecast: %s\r\n", s_wx.text);
    if (s_st.count > 1) {
        printf("Last hour:");
        for (int i = (s_st.count > 7 ? s_st.count - 7 : 0); i < s_st.count; i++)
            printf(" %u.%u", s_st.p[i] / 10, s_st.p[i] % 10);
        printf("\r\n");
    }
    fflush(stdout);
}
