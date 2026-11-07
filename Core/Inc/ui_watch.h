/** @file ui_watch.h
 *  Extra screens built in code (kept out of the SquareLine-generated files so
 *  re-exporting the UI never overwrites them) + swipe navigation.
 *
 *  Swipe left / right:  Watchface <-> Sensors <-> Compass <-> Stopwatch <-> Timer <-> Voice <-> Lights
 *  (Lights wraps around: one swipe right from the watchface)
 *  Swipe down: back to watchface.  Double tap on watchface: start/stop recording.
 */
#ifndef UI_WATCH_H
#define UI_WATCH_H

#include <stdbool.h>
#include <stdint.h>

void UI_Watch_Init(void);      /* call once after ui_init() */
void UI_Watch_Task(void);      /* call from main loop       */

/* Phone / nRF52 hooks */
void UI_ShowNotification(const char *text);   /* popup + buzz + wake screen */
void UI_SetWeather(const char *text);         /* shown on the sensors page  */
void UI_SetBleConnected(bool connected);
void UI_LightAck(bool ok);                    /* nRF52 confirmed the light broadcast */

/* Call after starting/stopping the recorder from outside the UI (serial cmd) */
void UI_RecorderChanged(void);

/* true while something needs the screen to stay on (compass cal / Set North) */
bool UI_KeepAwake(void);

#endif
