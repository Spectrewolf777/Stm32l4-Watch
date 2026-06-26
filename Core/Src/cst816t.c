/* CST816T capacitive touch-screen driver for STM32 HAL. */

#include "cst816t.h"
#include <string.h>
#include <stdio.h>

#define REG_GESTURE_ID      0x01
#define REG_FINGER_NUM      0x02
#define REG_XPOS_H          0x03
#define REG_XPOS_L          0x04
#define REG_YPOS_H          0x05
#define REG_YPOS_L          0x06
#define REG_CHIP_ID         0xA7
#define REG_PROJ_ID         0xA8
#define REG_FW_VERSION      0xA9
#define REG_FACTORY_ID      0xAA
#define REG_SLEEP_MODE      0xE5
#define REG_IRQ_CTL         0xFA
#define REG_LONG_PRESS_TICK 0xEB
#define REG_MOTION_MASK     0xEC
#define REG_DIS_AUTOSLEEP   0xFE

#define MOTION_MASK_CONTINUOUS_LEFT_RIGHT 0b100
#define MOTION_MASK_CONTINUOUS_UP_DOWN    0b010
#define MOTION_MASK_DOUBLE_CLICK          0b001

#define IRQ_EN_TOUCH      0x40
#define IRQ_EN_CHANGE     0x20
#define IRQ_EN_MOTION     0x10
#define IRQ_EN_LONGPRESS  0x01

