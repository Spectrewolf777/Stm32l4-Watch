#ifndef RTC_TASKS_H
#define RTC_TASKS_H

#include "main.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// One single function call to manage the watch display and time updates
void rtc_tasks(void *rtc_handle);

// true while the backlight is on (used by the touch input to swallow wake taps)
bool Display_IsOn(void);

// Turn the screen on from code (notifications, alarms). Safe from anywhere.
void Display_Wake(void);

#ifdef __cplusplus
}
#endif

#endif /* RTC_TASKS_H */