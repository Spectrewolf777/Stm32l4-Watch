/**
  ******************************************************************************
  * @file    lsm6dsv16x_stepcounter.h
  * @brief   Application-layer wrapper around the LSM6DSV16XTR's embedded
  *          pedometer / step-counter feature AND its embedded tilt
  *          detector (the "raise wrist to look at watch" gesture).
  *
  *          Requires lsm6dsv16x_reg.c / lsm6dsv16x_reg.h (official
  *          STMicroelectronics platform-independent driver) to be added
  *          to the project.
  ******************************************************************************
  */
#ifndef LSM6DSV16X_STEPCOUNTER_H
#define LSM6DSV16X_STEPCOUNTER_H

#include "stm32l4xx_hal.h" // Update this to your specific STM32 family (e.g., stm32l4xx_hal.h)
#include <stdbool.h>
#include <stdint.h>
#include "main.h"   /* brings in I2C_HandleTypeDef for your MCU family */
 
#ifdef __cplusplus
extern "C" {
#endif
 
/**
  * @brief  Bring up the LSM6DSV16X and enable its embedded step counter.
  *
  * Performs: WHO_AM_I check, software reset, BDU enable, gyro left off
  * (not needed, saves power), accelerometer ODR/full-scale config, and
  * enables the pedometer algorithm with false-step rejection.
  *
  * @param  hi2c  I2C handle the LSM6DSV16X is wired to (e.g. &hi2c1)
  * @retval true if the sensor answered WHO_AM_I correctly and was configured
  */
bool StepCounter_Init(I2C_HandleTypeDef *hi2c);
 
/**
  * @brief  Read the number of steps counted since the last reset/reboot.
  * @retval step count, or 0 if StepCounter_Init() never succeeded
  */
uint16_t StepCounter_GetSteps(void);
 
/**
  * @brief  Reset the embedded step counter back to 0.
  * @retval true on success
  */
bool StepCounter_Reset(void);
 
#ifdef __cplusplus
}
#endif
 
#endif /* LSM6DSV16X_STEPCOUNTER_H */