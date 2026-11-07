/** @file hdc2010.h  HDC2010 humidity + temperature (on-demand trigger mode). */
#ifndef HDC2010_H
#define HDC2010_H

#include <stdbool.h>
#include <stdint.h>

bool HDC2010_Init(void);
bool HDC2010_Present(void);

/* Non-blocking: Trigger(), then Read() >= 2 ms later. */
bool HDC2010_Trigger(void);
bool HDC2010_Read(int16_t *temp_x10, uint16_t *rh_x10);

#endif
