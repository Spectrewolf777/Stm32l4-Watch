/** @file drv2605l.h  DRV2605L haptic driver, internal-trigger library effects.
 *
 *  Set DRV2605L_USE_LRA to 1 if the motor is an LRA (coin type that buzzes
 *  at one frequency), 0 for an ERM (spinning eccentric mass).
 *  EN = PB5 (100k pull-down on board) is driven high by DRV2605L_Init(),
 *  IN/TRIG = PB4 is held low (effects are triggered over I2C). */
#ifndef DRV2605L_H
#define DRV2605L_H

#include <stdbool.h>
#include <stdint.h>

#ifndef DRV2605L_USE_LRA
#define DRV2605L_USE_LRA  0
#endif

/* A few useful effects from the TI ROM library (datasheet table 11.2) */
#define HAPTIC_CLICK        1    /* strong click 100%        */
#define HAPTIC_DOUBLE_CLICK 10   /* double click 100%        */
#define HAPTIC_TICK         24   /* sharp tick 100%          */
#define HAPTIC_BUZZ         47   /* buzz 1 100%              */
#define HAPTIC_ALERT        16   /* 1000 ms alert            */

bool DRV2605L_Init(void);
bool DRV2605L_Present(void);
bool DRV2605L_Play(uint8_t effect);
/* Up to 8 steps: effect numbers, or (0x80 | n) for an n*10 ms pause */
bool DRV2605L_PlaySequence(const uint8_t *seq, uint8_t n);
#define HAPTIC_WAIT(ms)     (0x80 | ((ms) / 10))
#define HAPTIC_ALERT_750MS  15
void DRV2605L_Stop(void);
/* Call every ~50 ms: puts the chip back in standby once an effect ends */
void DRV2605L_Service(void);

#endif
