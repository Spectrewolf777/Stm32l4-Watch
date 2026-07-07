/**
  ******************************************************************************
  * @file    wrist_wake.h
  * @brief   Wake-on-wrist-tilt gesture, using the LSM6DSV16X's embedded
  *          "tilt" detector so the STM32 doesn't have to run its own
  *          orientation-change algorithm - the sensor raises an interrupt
  *          on INT1 whenever you rotate the watch up to look at it.
  *
  * IMPORTANT ordering: call StepCounter_Init() first! This module does
  * NOT reset or reconfigure the accelerometer ODR/full-scale - it assumes
  * the sensor is already up and running (from step_counter.c) and only
  * *adds* the tilt feature + INT1 routing on top, so the pedometer keeps
  * counting undisturbed.
  ******************************************************************************
  */
#ifndef WRIST_WAKE_H
#define WRIST_WAKE_H

#include <stdbool.h>
#include <stdint.h>
#include "main.h"   /* brings in I2C_HandleTypeDef for your MCU family */

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Enable the embedded tilt detector and route it to INT1.
  *
  * Call this AFTER StepCounter_Init(&hi2c1) has already returned true.
  *
  * @param  hi2c  I2C handle the LSM6DSV16X is wired to (e.g. &hi2c1)
  * @retval true if the tilt engine was enabled successfully
  */
bool WristWake_Init(I2C_HandleTypeDef *hi2c);

/**
  * @brief  Call this from HAL_GPIO_EXTI_Callback() when
  *         GPIO_Pin == IMU_INT1_Pin. ISR-safe: it only sets a flag,
  *         it does no I2C bus traffic from interrupt context.
  */
void WristWake_OnExti(void);

/**
  * @brief  Call from the main loop to service a pending interrupt.
  *
  * Reads the embedded-function status register over I2C (which also
  * clears the sensor's latched interrupt), and reports whether the
  * pending interrupt was actually a tilt/wake event.
  *
  * @retval true exactly once per confirmed wrist-tilt gesture
  */
bool WristWake_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* WRIST_WAKE_H */