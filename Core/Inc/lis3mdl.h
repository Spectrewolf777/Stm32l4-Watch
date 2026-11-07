/** @file lis3mdl.h  LIS3MDL magnetometer (behind the LSM6DSV16X sensor hub)
 *  + compass heading with optional tilt compensation.
 *
 *  1. Calibrate: spin the watch flat 360 deg (required). Then tilt / flip it
 *     every way (optional) -> enables tilt compensation, found automatically.
 *  2. Set North with your phone compass (one tap with tilt comp, two without).
 *  Everything is saved to EEPROM. COMPASS_OFFSET_DEG only applies before
 *  Set North has been done. */
#ifndef LIS3MDL_H
#define LIS3MDL_H

#include <stdbool.h>
#include <stdint.h>

#define COMPASS_OFFSET_DEG  0

/* From the PCB layout: the LSM6DSV16X and LIS3MDL are on the SAME side of
 * the board and square to each other, so only the 4 "rotate by 90 deg"
 * axis arrangements are possible. Set to 0 if a future board puts them on
 * opposite sides (auto-detect then also tries the 4 flipped ones). */
#define COMPASS_SAME_PCB_SIDE  1

bool LIS3MDL_Init(void);
bool LIS3MDL_Present(void);
bool LIS3MDL_ReadRaw(int16_t xyz[3]);
const char *LIS3MDL_Info(void);   /* "OK at 0x1C (via IMU sensor hub)" etc */

/* Heading 0..359 degrees (0 = north), or -1 on error */
int16_t Compass_Heading(void);

/* Calibration: spin flat 360 (X/Y, required), then optionally tilt / flip
 * the watch every way (Z + accelerometer) to enable tilt compensation. */
typedef enum { CAL_FAILED, CAL_OK_FLAT, CAL_OK_TILT } compass_cal_result_t;
void Compass_CalStart(void);
compass_cal_result_t Compass_CalStop(bool save);  /* also clears Set North */
uint8_t Compass_CalProgress(void);       /* flat spin 0..100 (required)      */
uint8_t Compass_CalTiltProgress(void);   /* tilt/flip 0..100 (optional)      */
bool Compass_CalRunning(void);
void Compass_Service(void);              /* call ~20 Hz (sensors.c, 50 ms)   */
bool Compass_TiltCompOn(void);

/* Set North (after calibration, use your phone compass):
 *  - tilt comp on : point 12 o'clock north, Compass_AlignNorth() -> done
 *  - flat mode    : Compass_AlignNorth() at north, then turn 90 deg right
 *                   and Compass_AlignEast() (learns turning direction) */
typedef enum { ALIGN_OK, ALIGN_FAILED, ALIGN_NOT_90 } compass_align_result_t;
bool Compass_AlignNorth(void);
bool Compass_AlignNeedsEast(void);
compass_align_result_t Compass_AlignEast(void);
void Compass_AlignCancel(void);
bool Compass_AlignActive(void);
bool Compass_IsAligned(void);
void Compass_PrintDebug(void);           /* "-mag" serial command */

/* Gyro fusion (needs tilt calibration). Enable while the compass page is
 * shown (turns the gyro on), call the task every main-loop pass. */
void Compass_FusionEnable(bool on);
void Compass_FusionTask(void);
void Compass_FusionReset(void);
bool Compass_FusionActive(void);

#endif
