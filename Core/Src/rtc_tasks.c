#include "rtc_tasks.h"
#include "lvgl/lvgl.h"
#include "ui.h"
#include "rv_3028_c7.h"
#include <stdio.h>
#include <stdbool.h>
#include "wrist_wake.h"

static const char* days_of_week[] = { "", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static uint32_t display_turn_on_time = 0;
static bool displayOn = false;

// Forward declaration of an internal update helper
static void update_display_time(void *rtc_handle) {
    rv3028_time_t current_time;
    if (rv3028_get_time((rv3028_handle_t*)rtc_handle, &current_time) == HAL_OK) {
        char buf[16];
        uint8_t hr = current_time.hours % 12;
        snprintf(buf, sizeof(buf), "%d:%02d", hr == 0 ? 12 : hr, current_time.minutes);
        lv_label_set_text(ui_Time, buf);

        bool valid_day = (current_time.weekday >= 1 && current_time.weekday <= 7);
        lv_label_set_text(ui_dayName, valid_day ? days_of_week[current_time.weekday] : "---");
    }
}

void rtc_tasks(void *rtc_handle) {
    // 1. Check for wake gesture
    if (WristWake_Poll()) {
        update_display_time(rtc_handle);
        HAL_GPIO_WritePin(BL_GPIO_Port, BL_Pin, GPIO_PIN_SET);
        
        if (!displayOn) {
            display_turn_on_time = HAL_GetTick();
            displayOn = true;
        }
    }

    // 2. Handle 10-second timeout
    if (displayOn && (HAL_GetTick() - display_turn_on_time >= 10000)) {
        HAL_GPIO_WritePin(BL_GPIO_Port, BL_Pin, GPIO_PIN_RESET);
        displayOn = false;
    }
}