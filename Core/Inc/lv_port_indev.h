#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the CST816T as an LVGL pointer device.
 * Call after lv_port_disp_init() and CST816T_Init(). */
void lv_port_indev_init(void);

extern lv_indev_t *indev_touchpad;

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_INDEV_H */
