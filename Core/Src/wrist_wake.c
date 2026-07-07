/**
  ******************************************************************************
  * @file    wrist_wake.c
  * @brief   Wake-on-wrist-tilt gesture, using the LSM6DSV16X's embedded
  *          "tilt" detector (register-level access via ST's official
  *          lsm6dsv16x_reg.c/.h driver). Configuration mirrors ST's own
  *          validated "lsm6dsv16x_tilt.c" reference example.
  ******************************************************************************
  */
#include "wrist_wake.h"
#include "lsm6dsv16x_reg.h"

/* ---- module-private state ------------------------------------------------ */
static stmdev_ctx_t         s_ctx;
static I2C_HandleTypeDef   *s_hi2c        = NULL;
static bool                  s_ready       = false;
static volatile bool         s_irq_pending = false;

#define LSM6DSV16X_I2C_TIMEOUT_MS   100U

/* ---- platform glue: STM32 HAL I2C ----------------------------------------- */
/* Duplicated (rather than shared with step_counter.c) on purpose, so this
 * module stays a fully standalone, drop-in .c/.h pair. */
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

/* ---- public API ------------------------------------------------------------ */
bool WristWake_Init(I2C_HandleTypeDef *hi2c)
{
  uint8_t whoami = 0;
  lsm6dsv16x_emb_pin_int_route_t route = {0};

  s_ready = false;
  s_hi2c  = hi2c;

  s_ctx.write_reg = platform_write;
  s_ctx.read_reg  = platform_read;
  s_ctx.mdelay    = NULL; /* no boot delay here - StepCounter_Init already did one */
  s_ctx.handle    = s_hi2c;

  if (lsm6dsv16x_device_id_get(&s_ctx, &whoami) != 0)
  {
    return false;
  }
  if (whoami != LSM6DSV16X_ID)
  {
    return false;
  }

  /* Enable the embedded tilt algorithm */
  if (lsm6dsv16x_tilt_mode_set(&s_ctx, PROPERTY_ENABLE) != 0)
  {
    return false;
  }

  /* Read-modify-write the INT1 embedded-function routing so we don't
   * clobber any other embedded event (step detector, significant
   * motion, FSM...) that might already be routed to INT1. */
  if (lsm6dsv16x_emb_pin_int1_route_get(&s_ctx, &route) != 0)
  {
    return false;
  }
  route.tilt = PROPERTY_ENABLE;
  if (lsm6dsv16x_emb_pin_int1_route_set(&s_ctx, &route) != 0)
  {
    return false;
  }

  /* Latch the embedded interrupt so a brief tilt pulse isn't missed;
   * it's cleared automatically the moment WristWake_Poll() reads status. */
  lsm6dsv16x_embedded_int_cfg_set(&s_ctx, LSM6DSV16X_INT_LATCH_ENABLE);

  s_irq_pending = false;
  s_ready = true;
  return true;
}

void WristWake_OnExti(void)
{
  s_irq_pending = true;
}

bool WristWake_Poll(void)
{
  lsm6dsv16x_embedded_status_t status = {0};

  if (!s_ready || !s_irq_pending)
  {
    return false;
  }
  s_irq_pending = false; /* clear first: any re-trigger during the I2C read below is preserved for the next Poll() call */

  if (lsm6dsv16x_embedded_status_get(&s_ctx, &status) != 0)
  {
    return false;
  }

  return (status.tilt != 0);
}