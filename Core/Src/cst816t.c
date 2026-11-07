/**
  ******************************************************************************
  * @file    cst816t.c
  * @brief   CST816T touch driver - raw coordinates + MCU-side gestures.
  *          See cst816t.h for the sleep / wake strategy.
  ******************************************************************************
  */
#include "cst816t.h"
#include <stdlib.h>

/* ---- Registers ---------------------------------------------------------- */
#define CST816T_ADDR          (0x15 << 1)
#define REG_GESTURE_ID        0x01   /* burst read 0x01..0x06 */
#define REG_CHIP_ID           0xA7
#define REG_PROJ_ID           0xA8
#define REG_FW_VERSION        0xA9
#define REG_MOTION_MASK       0xEC
#define REG_IRQ_PULSE_WIDTH   0xED
#define REG_NOR_SCAN_PER      0xEE
#define REG_AUTO_SLEEP_TIME   0xF9
#define REG_IRQ_CTL           0xFA
#define REG_AUTO_RESET        0xFB
#define REG_LONG_PRESS_TIME   0xFC
#define REG_DIS_AUTO_SLEEP    0xFE

#define IRQ_EN_TOUCH          0x40   /* periodic pulse while finger down */
#define IRQ_EN_CHANGE         0x20   /* pulse on press / release         */

#define EVT_LIFT_UP           1      /* XposH bits 7:6 */

#define I2C_TIMEOUT_MS        10U
#define POLL_WHILE_PRESSED_MS 20U    /* catch release if an INT is missed */

/* USB debug print lives in main.c */
extern void USB_Printf(const char *format, ...);

/* ---- State -------------------------------------------------------------- */
static I2C_HandleTypeDef *s_hi2c;
static cst816t_info_t     s_info;

static volatile bool      s_irq_pending;
static volatile bool      s_activity;

static bool     s_pressed;
static uint16_t s_x, s_y;
static uint32_t s_last_read_tick;

static bool     s_cfg_done;
static bool     s_autosleep_want = true;
static bool     s_autosleep_set;
static bool     s_autosleep_known;

/* gesture tracking */
static uint16_t s_start_x, s_start_y;
static uint16_t s_max_move;
static uint32_t s_press_tick;
static bool     s_long_fired;
static bool     s_ignore_touch;
static uint32_t s_ignore_tick;
static uint32_t s_last_tap_tick;
static cst816t_gesture_t s_gesture;

static bool     s_debug;

/* ---- Low-level I2C ------------------------------------------------------ */
static bool reg_write(uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(s_hi2c, CST816T_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                             &val, 1, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool reg_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(s_hi2c, CST816T_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                            buf, len, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool apply_config(void)
{
    bool ok = true;
    ok &= reg_write(REG_IRQ_CTL, IRQ_EN_TOUCH | IRQ_EN_CHANGE);
    ok &= reg_write(REG_NOR_SCAN_PER, 1);       /* 10 ms scan             */
    ok &= reg_write(REG_IRQ_PULSE_WIDTH, 10);   /* 1 ms INT pulse         */
    ok &= reg_write(REG_MOTION_MASK, 0);        /* chip gestures unused   */
    ok &= reg_write(REG_AUTO_SLEEP_TIME, 2);    /* sleep 2 s after touch  */
    ok &= reg_write(REG_AUTO_RESET, 0);         /* don't self-reset while */
    ok &= reg_write(REG_LONG_PRESS_TIME, 0);    /*   finger is held       */
    return ok;
}

static void apply_autosleep(void)
{
    if (s_autosleep_known && s_autosleep_set == s_autosleep_want) return;
    if (reg_write(REG_DIS_AUTO_SLEEP, s_autosleep_want ? 0x00 : 0x01)) {
        s_autosleep_set   = s_autosleep_want;
        s_autosleep_known = true;
    }
}

static void map_point(uint16_t rx, uint16_t ry, uint16_t *ox, uint16_t *oy)
{
#if CST816T_SWAP_XY
    uint16_t t = rx; rx = ry; ry = t;
#endif
    if (rx >= CST816T_RES_X) rx = CST816T_RES_X - 1;
    if (ry >= CST816T_RES_Y) ry = CST816T_RES_Y - 1;
#if CST816T_MIRROR_X
    rx = (CST816T_RES_X - 1) - rx;
#endif
#if CST816T_MIRROR_Y
    ry = (CST816T_RES_Y - 1) - ry;
#endif
    *ox = rx;
    *oy = ry;
}

/* ---- Gesture maths ------------------------------------------------------ */
static void emit(cst816t_gesture_t g)
{
    if (s_ignore_touch) return;
    s_gesture = g;
    if (s_debug) USB_Printf("[touch] %s\r\n", CST816T_GestureName(g));
}

static void on_press(uint16_t x, uint16_t y)
{
    s_start_x    = x;
    s_start_y    = y;
    s_max_move   = 0;
    s_press_tick = HAL_GetTick();
    s_long_fired = false;
    if (HAL_GetTick() - s_ignore_tick > 100U) s_ignore_touch = false;
    s_activity   = true;
    if (s_debug) USB_Printf("[touch] down %u,%u\r\n", x, y);
}

static void on_move(uint16_t x, uint16_t y)
{
    uint16_t dx = (uint16_t)abs((int)x - (int)s_start_x);
    uint16_t dy = (uint16_t)abs((int)y - (int)s_start_y);
    uint16_t d  = dx > dy ? dx : dy;
    if (d > s_max_move) s_max_move = d;

    if (!s_long_fired && s_max_move <= CST816T_TAP_MAX_MOVE_PX &&
        HAL_GetTick() - s_press_tick >= CST816T_LONG_PRESS_MS) {
        s_long_fired = true;
        emit(CST816T_GESTURE_LONG_PRESS);
    }
}

static void on_release(void)
{
    int dx = (int)s_x - (int)s_start_x;
    int dy = (int)s_y - (int)s_start_y;
    int adx = abs(dx), ady = abs(dy);

    if (s_debug) USB_Printf("[touch] up %u,%u\r\n", s_x, s_y);

    if (s_long_fired) return;

    if (adx >= (int)CST816T_SWIPE_MIN_PX || ady >= (int)CST816T_SWIPE_MIN_PX) {
        if (adx > ady) emit(dx > 0 ? CST816T_GESTURE_SWIPE_RIGHT : CST816T_GESTURE_SWIPE_LEFT);
        else           emit(dy > 0 ? CST816T_GESTURE_SWIPE_DOWN  : CST816T_GESTURE_SWIPE_UP);
        s_last_tap_tick = 0;
    } else if (s_max_move <= CST816T_TAP_MAX_MOVE_PX) {
        uint32_t now = HAL_GetTick();
        if (s_last_tap_tick && now - s_last_tap_tick <= CST816T_DOUBLE_TAP_MS) {
            emit(CST816T_GESTURE_DOUBLE_TAP);
            s_last_tap_tick = 0;
        } else {
            emit(CST816T_GESTURE_TAP);
            s_last_tap_tick = now;
        }
    }
}

/* ---- Public API --------------------------------------------------------- */
bool CST816T_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t id = 0;

    s_hi2c = hi2c;
    s_info.present = false;

    /* Hardware reset */
    HAL_GPIO_WritePin(TP_RST_GPIO_Port, TP_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(TP_RST_GPIO_Port, TP_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(60);

    for (int i = 0; i < 5 && !s_info.present; i++) {
        if (reg_read(REG_CHIP_ID, &id, 1) && id != 0x00 && id != 0xFF) {
            s_info.present = true;
            s_info.chip_id = id;
        } else {
            HAL_Delay(10);
        }
    }

    if (s_info.present) {
        reg_read(REG_PROJ_ID, &s_info.proj_id, 1);
        reg_read(REG_FW_VERSION, &s_info.fw_version, 1);
        s_cfg_done = apply_config();
        apply_autosleep();
    }

    s_irq_pending = false;
    s_activity    = false;
    s_pressed     = false;
    return s_info.present;
}

void CST816T_OnExti(void)
{
    s_irq_pending = true;
    s_activity    = true;
}

void CST816T_Process(void)
{
    uint8_t  b[6];
    uint32_t now = HAL_GetTick();

    if (s_hi2c == NULL) return;

    bool poll = s_pressed && (now - s_last_read_tick >= POLL_WHILE_PRESSED_MS);
    if (!s_irq_pending && !poll) return;
    s_irq_pending    = false;
    s_last_read_tick = now;

    if (!reg_read(REG_GESTURE_ID, b, sizeof(b))) {
        /* Chip asleep / NACK: no finger on the panel */
        if (s_pressed) {
            s_pressed = false;
            on_release();
        }
        return;
    }

    /* The chip answered, so it's awake: finish any pending configuration */
    if (!s_info.present) {
        s_info.present = true;
        reg_read(REG_CHIP_ID, &s_info.chip_id, 1);
        reg_read(REG_FW_VERSION, &s_info.fw_version, 1);
    }
    if (!s_cfg_done) s_cfg_done = apply_config();
    apply_autosleep();

    uint8_t  fingers = b[1];
    uint8_t  event   = b[2] >> 6;
    uint16_t rx      = (uint16_t)(((b[2] & 0x0F) << 8) | b[3]);
    uint16_t ry      = (uint16_t)(((b[4] & 0x0F) << 8) | b[5]);
    bool     down    = (fingers != 0) && (event != EVT_LIFT_UP);

    if (down) {
        map_point(rx, ry, &s_x, &s_y);
        if (!s_pressed) {
            s_pressed = true;
            on_press(s_x, s_y);
        }
        on_move(s_x, s_y);
    } else if (s_pressed) {
        s_pressed = false;
        on_release();
    }
}

bool CST816T_GetPoint(uint16_t *x, uint16_t *y)
{
    if (x) *x = s_x;
    if (y) *y = s_y;
    return s_pressed;
}

cst816t_gesture_t CST816T_GetGesture(void)
{
    cst816t_gesture_t g = s_gesture;
    s_gesture = CST816T_GESTURE_NONE;
    return g;
}

bool CST816T_ActivityPending(void)
{
    bool a = s_activity;
    s_activity = false;
    return a;
}

void CST816T_SetAutoSleep(bool enable)
{
    s_autosleep_want = enable;
    if (s_hi2c) apply_autosleep();
}

const cst816t_info_t *CST816T_GetInfo(void)
{
    return &s_info;
}

void CST816T_IgnoreCurrentTouch(void)
{
    s_ignore_touch  = true;
    s_ignore_tick   = HAL_GetTick();
    s_last_tap_tick = 0;
}

void CST816T_SetDebug(bool on)
{
    s_debug = on;
}

const char *CST816T_GestureName(cst816t_gesture_t g)
{
    switch (g) {
    case CST816T_GESTURE_SWIPE_UP:    return "swipe up";
    case CST816T_GESTURE_SWIPE_DOWN:  return "swipe down";
    case CST816T_GESTURE_SWIPE_LEFT:  return "swipe left";
    case CST816T_GESTURE_SWIPE_RIGHT: return "swipe right";
    case CST816T_GESTURE_TAP:         return "tap";
    case CST816T_GESTURE_DOUBLE_TAP:  return "double tap";
    case CST816T_GESTURE_LONG_PRESS:  return "long press";
    default:                          return "none";
    }
}
