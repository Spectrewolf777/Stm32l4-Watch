/**
  ******************************************************************************
  * @file    cst816t.h
  * @brief   CST816T capacitive touch controller driver (STM32L4 HAL, I2C2).
  *
  * The CST816T on this 1.28" GC9A01 module reports raw X/Y but its on-chip
  * gesture register is unreliable / empty on this firmware revision, so all
  * gestures (tap, double tap, long press, swipes) are worked out here on the
  * MCU from the raw coordinates. The chip's own gesture byte is ignored.
  *
  * Sleep behaviour:
  *  - Screen ON : auto-sleep disabled -> chip always answers on I2C.
  *  - Screen OFF: auto-sleep enabled  -> chip drops into low-power scan and
  *                stops answering I2C (NACKs). A touch wakes it and it pulses
  *                TP_INT, which is what wakes the watch screen.
  *  All I2C errors are tolerated, nothing here blocks forever.
  ******************************************************************************
  */
#ifndef CST816T_H
#define CST816T_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Panel resolution and coordinate correction (set if touch looks mirrored) */
#define CST816T_RES_X        240
#define CST816T_RES_Y        240
#define CST816T_SWAP_XY      0
#define CST816T_MIRROR_X     0
#define CST816T_MIRROR_Y     0

/* Software gesture tuning */
#define CST816T_SWIPE_MIN_PX        40U   /* min travel for a swipe          */
#define CST816T_TAP_MAX_MOVE_PX     15U   /* max travel still counted as tap */
#define CST816T_LONG_PRESS_MS      700U   /* hold time for a long press      */
#define CST816T_DOUBLE_TAP_MS      300U   /* max gap between taps            */

typedef enum {
    CST816T_GESTURE_NONE = 0,
    CST816T_GESTURE_SWIPE_UP,
    CST816T_GESTURE_SWIPE_DOWN,
    CST816T_GESTURE_SWIPE_LEFT,
    CST816T_GESTURE_SWIPE_RIGHT,
    CST816T_GESTURE_TAP,
    CST816T_GESTURE_DOUBLE_TAP,
    CST816T_GESTURE_LONG_PRESS,
} cst816t_gesture_t;

typedef struct {
    uint8_t chip_id;     /* 0xB5 = CST816T, 0xB4 = CST816S, 0xB6 = CST816D */
    uint8_t proj_id;
    uint8_t fw_version;
    bool    present;
} cst816t_info_t;

/** Reset + configure the controller. Never blocks forever.
 *  @retval true if the chip answered. Touch still works later if false
 *          (e.g. it was asleep), the driver keeps retrying config. */
bool CST816T_Init(I2C_HandleTypeDef *hi2c);

/** Call from HAL_GPIO_EXTI_Callback() for TP_INT_Pin. ISR-safe (flag only). */
void CST816T_OnExti(void);

/** Call every main-loop iteration. Does the I2C reads and gesture maths. */
void CST816T_Process(void);

/** Current finger state. Returns true while pressed; x/y always hold the
 *  last known point (may be NULL). */
bool CST816T_GetPoint(uint16_t *x, uint16_t *y);

/** Pops the last completed software gesture (NONE if nothing new). */
cst816t_gesture_t CST816T_GetGesture(void);

/** Returns true (once) if any touch activity happened since last call:
 *  an INT pulse or a new press. Used for touch-to-wake and to keep the
 *  screen awake while it's being used. */
bool CST816T_ActivityPending(void);

/** true  = let the chip auto-sleep (screen off, wake on touch)
 *  false = keep the chip awake (screen on, fast response)
 *  Applied immediately if possible, otherwise retried on next Process(). */
void CST816T_SetAutoSleep(bool enable);

const cst816t_info_t *CST816T_GetInfo(void);
const char *CST816T_GestureName(cst816t_gesture_t g);

/** Don't report any gesture for the finger that is currently down
 *  (used for the touch that wakes the screen). */
void CST816T_IgnoreCurrentTouch(void);

/** Print every touch / gesture over USB CDC (for testing). */
void CST816T_SetDebug(bool on);

#ifdef __cplusplus
}
#endif

#endif /* CST816T_H */
