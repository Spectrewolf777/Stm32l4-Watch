/**
 * @file lv_port_indev.c
 * CST816T -> LVGL pointer input.
 *
 * While the screen is off every touch is hidden from LVGL, and the touch that
 * wakes the screen stays hidden until the finger lifts, so waking the watch
 * can never press a button by accident.
 */
#include "lv_port_indev.h"
#include "cst816t.h"
#include "rtc_tasks.h"

lv_indev_t *indev_touchpad = NULL;

static bool s_block_until_release = true;

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;
    bool pressed = CST816T_GetPoint(&x, &y);

    if (!Display_IsOn()) {
        s_block_until_release = true;
    } else if (s_block_until_release && !pressed) {
        s_block_until_release = false;
    }

    data->point.x = (int32_t)x;
    data->point.y = (int32_t)y;
    data->state   = (pressed && !s_block_until_release) ? LV_INDEV_STATE_PRESSED
                                                        : LV_INDEV_STATE_RELEASED;
}

void lv_port_indev_init(void)
{
    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
}
