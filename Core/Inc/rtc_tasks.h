#ifndef RTC_TASKS_H
#define RTC_TASKS_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

// One single function call to manage the watch display and time updates
void rtc_tasks(void *rtc_handle);

#ifdef __cplusplus
}
#endif

#endif /* RTC_TASKS_H */