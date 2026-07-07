#include "lsm6dsv16x_stepcounter.h"
#include "lsm6dsv16x_reg.h"

/* ---- module-private state ------------------------------------------------ */
static stmdev_ctx_t       s_ctx;
static I2C_HandleTypeDef *s_hi2c  = NULL;
static bool                s_ready = false;
 
#define LSM6DSV16X_BOOT_TIME_MS     10U
#define LSM6DSV16X_I2C_TIMEOUT_MS   100U
 
/* ---- platform glue: STM32 HAL I2C ---------------------------------------- */
static int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
  HAL_StatusTypeDef status = HAL_I2C_Mem_Write((I2C_HandleTypeDef *)handle,
                                                LSM6DSV16X_I2C_ADD_L, reg,
                                                I2C_MEMADD_SIZE_8BIT,
                                                (uint8_t *)bufp, len,
                                                LSM6DSV16X_I2C_TIMEOUT_MS);
  return (status == HAL_OK) ? 0 : -1;
}
 
static int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read((I2C_HandleTypeDef *)handle,
                                               LSM6DSV16X_I2C_ADD_L, reg,
                                               I2C_MEMADD_SIZE_8BIT,
                                               bufp, len,
                                               LSM6DSV16X_I2C_TIMEOUT_MS);
  return (status == HAL_OK) ? 0 : -1;
}
 
static void platform_delay(uint32_t ms)
{
  HAL_Delay(ms);
}
 
/* ---- public API ----------------------------------------------------------- */
bool StepCounter_Init(I2C_HandleTypeDef *hi2c)
{
  uint8_t whoami = 0;
  lsm6dsv16x_stpcnt_mode_t stpcnt_mode = {0};
 
  s_ready = false;
  s_hi2c  = hi2c;
 
  s_ctx.write_reg = platform_write;
  s_ctx.read_reg  = platform_read;
  s_ctx.mdelay    = platform_delay;
  s_ctx.handle    = s_hi2c;
 
  platform_delay(LSM6DSV16X_BOOT_TIME_MS);
 
  if (lsm6dsv16x_device_id_get(&s_ctx, &whoami) != 0)
  {
    return false;
  }
  if (whoami != LSM6DSV16X_ID)
  {
    return false; /* wrong device or not answering on this bus/address */
  }
 
  /* Software reset -> known default state */
  if (lsm6dsv16x_sw_por(&s_ctx) != 0)
  {
    return false;
  }
  platform_delay(LSM6DSV16X_BOOT_TIME_MS);
 
  /* Block Data Update: output registers don't change mid-read */
  lsm6dsv16x_block_data_update_set(&s_ctx, PROPERTY_ENABLE);
 
  /* Gyro isn't needed for pedometer/tilt - leave it off to save power */
  lsm6dsv16x_gy_data_rate_set(&s_ctx, LSM6DSV16X_ODR_OFF);
 
  /* Accelerometer config. 120 Hz / 2g are the values ST validated in its
   * own pedometer & tilt reference examples. If you want to shave power
   * for a wearable you can try a lower ODR (e.g. LSM6DSV16X_ODR_AT_30Hz) -
   * the embedded engines are designed to run across a range of ODRs, but
   * only 120 Hz is what ST's examples explicitly test. */
  lsm6dsv16x_xl_full_scale_set(&s_ctx, LSM6DSV16X_2g);
  lsm6dsv16x_xl_data_rate_set(&s_ctx, LSM6DSV16X_ODR_AT_120Hz);
 
  /* Enable the embedded pedometer, with false-step rejection */
  stpcnt_mode.step_counter_enable = PROPERTY_ENABLE;
  stpcnt_mode.false_step_rej      = PROPERTY_ENABLE;
  if (lsm6dsv16x_stpcnt_mode_set(&s_ctx, stpcnt_mode) != 0)
  {
    return false;
  }
 
  s_ready = true;
  return true;
}
 
uint16_t StepCounter_GetSteps(void)
{
  uint16_t steps = 0;
 
  if (!s_ready)
  {
    return 0;
  }
  if (lsm6dsv16x_stpcnt_steps_get(&s_ctx, &steps) != 0)
  {
    return 0;
  }
  return steps;
}
 
bool StepCounter_Reset(void)
{
  if (!s_ready)
  {
    return false;
  }
  /* Pulse the reset bit: set then clear, per ST's reset-step procedure */
  if (lsm6dsv16x_stpcnt_rst_step_set(&s_ctx, PROPERTY_ENABLE) != 0)
  {
    return false;
  }
  return (lsm6dsv16x_stpcnt_rst_step_set(&s_ctx, PROPERTY_DISABLE) == 0);
}
 