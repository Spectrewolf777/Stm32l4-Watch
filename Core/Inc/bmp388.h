/** @file bmp388.h  BMP388 barometer, normal mode ~1.5 Hz, x8 pressure OSR. */
#ifndef BMP388_H
#define BMP388_H

#include <stdbool.h>
#include <stdint.h>

bool BMP388_Init(void);
bool BMP388_Present(void);

/* pressure in Pa, temperature in 0.1 degC */
bool BMP388_Read(uint32_t *pressure_pa, int16_t *temp_x10);

/* Altitude in metres from pressure; sea-level reference defaults to
 * 101325 Pa, set it to your local QNH for accurate absolute altitude. */
int32_t BMP388_AltitudeM(uint32_t pressure_pa);
void    BMP388_SetSeaLevel(uint32_t pa);

#endif
