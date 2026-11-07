/** @file power_mon.h
 *  PA5 PWR_ST   : LM66200 ST pin (open drain, internal pull-up enabled).
 *                 VIN1 = battery, VIN2 = USB. Datasheet 7.3.3:
 *                 "powering the output using VIN1 -> ST pulled high,
 *                  using VIN2 -> ST pulled low" (also low on a fault).
 *                 So ST LOW = running from USB.
 *  PA6 BAT_ALERT: MAX17048 ALRT (open-drain, active low) -> low battery */
#ifndef POWER_MON_H
#define POWER_MON_H

#include <stdbool.h>
#include <stdint.h>

#define PWR_ST_USB_LEVEL  0   /* ST low = VIN2 (USB) powering the watch */

void Power_Init(void);
void Power_OnExti(uint16_t pin);   /* from HAL_GPIO_EXTI_Callback */
void Power_Task(void);             /* main loop */
bool Power_OnUsb(void);
uint8_t Power_StRaw(void);

#endif
