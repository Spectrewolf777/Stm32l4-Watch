#include "rtc_tasks.h"
#include "lvgl/lvgl.h"
#include "ui.h"
#include "rv_3028_c7.h"
#include <stdio.h>
#include <stdbool.h>
#include "cst816t.h"
#include "m24512.h"
#include "ui_watch.h"

#define TIME_REFRESH_MS  1000U

static const char* days_of_week[] = { "", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static uint32_t display_turn_on_time = 0;
static uint32_t last_time_refresh = 0;
static bool displayOn = false;
static volatile bool wake_request = false;

static void update_display_time(void *rtc_handle) {
    rv3028_time_t current_time;
    if (rv3028_get_time((rv3028_handle_t*)rtc_handle, &current_time) == HAL_OK) {
        char buf[16];
        uint8_t hr = current_time.hours % 12;
        snprintf(buf, sizeof(buf), "%d:%02d", hr == 0 ? 12 : hr, current_time.minutes);
        lv_label_set_text(ui_Time, buf);

        bool valid_day = (current_time.weekday >= 1 && current_time.weekday <= 7);
        lv_label_set_text(ui_dayName, valid_day ? days_of_week[current_time.weekday] : "---");

        snprintf(buf, sizeof(buf), "%d/%02d/20%02d", current_time.date, current_time.month,
                 current_time.year);
        lv_label_set_text(ui_date, buf);
    }
}

bool Display_IsOn(void) {
    return displayOn;
}

void Display_Wake(void) {
    wake_request = true;
}

static void display_on(void *rtc_handle) {
    update_display_time(rtc_handle);
    last_time_refresh = HAL_GetTick();
    HAL_GPIO_WritePin(BL_GPIO_Port, BL_Pin, GPIO_PIN_SET);
    displayOn = true;
    CST816T_SetAutoSleep(false);    // keep touch chip responsive
}

void rtc_tasks(void *rtc_handle) {
    // 1. Touch activity: wakes the screen when off, keeps it on while in use
    if (CST816T_ActivityPending()) {
        if (!displayOn) {
            display_on(rtc_handle);
            CST816T_IgnoreCurrentTouch();   // waking tap isn't a gesture
        }
        display_turn_on_time = HAL_GetTick();
    }

    // 2. Wake requested by code (notification, timer alarm...)
    if (wake_request) {
        wake_request = false;
        if (!displayOn) display_on(rtc_handle);
        display_turn_on_time = HAL_GetTick();
    }

    // 3. Keep the clock ticking while visible
    if (displayOn && HAL_GetTick() - last_time_refresh >= TIME_REFRESH_MS) {
        last_time_refresh = HAL_GetTick();
        update_display_time(rtc_handle);
    }

    // 4. Things that need the screen kept on (compass calibration / Set North)
    if (displayOn && UI_KeepAwake()) {
        display_turn_on_time = HAL_GetTick();
    }

    // 5. Timeout (don't turn off while a finger is still down)
    uint32_t timeout_ms = (uint32_t)Settings_Get()->screen_timeout_s * 1000U;
    if (displayOn && !CST816T_GetPoint(NULL, NULL) &&
        (HAL_GetTick() - display_turn_on_time >= timeout_ms)) {
        HAL_GPIO_WritePin(BL_GPIO_Port, BL_Pin, GPIO_PIN_RESET);
        displayOn = false;
        CST816T_SetAutoSleep(true);         // low-power scan, wakes on touch
    }
}
