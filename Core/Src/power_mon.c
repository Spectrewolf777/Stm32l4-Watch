#include "power_mon.h"
#include "main.h"
#include "max17048.h"
#include "sensors.h"
#include "drv2605l.h"
#include "rtc_tasks.h"
#include "ui_watch.h"

static volatile bool s_pwr_changed;
static volatile bool s_alert;
static bool          s_on_usb;

void Power_Init(void)
{
    GPIO_InitTypeDef g = {0};

    /* CubeMX generated these as rising edge / no pull. Correct them:
     * PWR_ST: both edges (plug + unplug), pull-up for an open-drain status pin
     * BAT_ALERT: falling edge, pull-up (MAX17048 ALRT is open-drain, active low) */
    g.Pin  = PWR_ST_Pin;
    g.Mode = GPIO_MODE_IT_RISING_FALLING;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(PWR_ST_GPIO_Port, &g);

    g.Pin  = BAT_ALERT_Pin;
    g.Mode = GPIO_MODE_IT_FALLING;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BAT_ALERT_GPIO_Port, &g);

    s_on_usb = (Power_StRaw() == PWR_ST_USB_LEVEL);

    /* An alert may already be latched from before reset */
    if (HAL_GPIO_ReadPin(BAT_ALERT_GPIO_Port, BAT_ALERT_Pin) == GPIO_PIN_RESET)
        s_alert = true;
}

void Power_OnExti(uint16_t pin)
{
    if (pin == PWR_ST_Pin)    s_pwr_changed = true;
    if (pin == BAT_ALERT_Pin) s_alert = true;
}

uint8_t Power_StRaw(void)
{
    return HAL_GPIO_ReadPin(PWR_ST_GPIO_Port, PWR_ST_Pin) == GPIO_PIN_SET ? 1 : 0;
}

bool Power_OnUsb(void) { return s_on_usb; }

void Power_Task(void)
{
    if (s_pwr_changed) {
        s_pwr_changed = false;
        HAL_Delay(5);                                   /* let it settle */
        bool usb = (Power_StRaw() == PWR_ST_USB_LEVEL);
        if (usb != s_on_usb) {
            s_on_usb = usb;
            Display_Wake();
            Haptic(HAPTIC_TICK);
        }
    }

    if (s_alert) {
        s_alert = false;
        uint8_t flags = MAX17048_ServiceAlert();
        if (!Sensors_Get()->bat_present) {
            /* no cell fitted: the gauge is reading the charger, ignore */
        } else if (flags & MAX17048_ALERT_SOC_LOW) {
            UI_ShowNotification("Battery low");
        } else if (flags & MAX17048_ALERT_VLOW) {
            UI_ShowNotification("Battery voltage low");
        }
    }
}
