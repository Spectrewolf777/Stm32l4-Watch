#include "hdc2010.h"
#include "i2c_dev.h"

#define REG_TEMP_L       0x00
#define REG_RESET_DRDY   0x0E
#define REG_MEAS_CONFIG  0x0F
#define REG_MANUF_ID_L   0xFC
#define REG_DEVICE_ID_L  0xFE

#define MANUF_ID         0x5449   /* "TI" */
#define DEVICE_ID        0x07D0

static i2c_dev_t s_dev;

bool HDC2010_Init(void)
{
    static const uint8_t addr[] = { 0x40, 0x41 };
    uint8_t id[4];

    if (!I2CDev_Find(&s_dev, addr, sizeof(addr))) return false;

    if (!I2CDev_Read(&s_dev, REG_MANUF_ID_L, id, 4) ||
        (uint16_t)(id[0] | (id[1] << 8)) != MANUF_ID ||
        (uint16_t)(id[2] | (id[3] << 8)) != DEVICE_ID) {
        s_dev.bus = NULL;
        return false;
    }

    I2CDev_WriteByte(&s_dev, REG_RESET_DRDY, 0x80);   /* soft reset          */
    HAL_Delay(2);
    I2CDev_WriteByte(&s_dev, REG_RESET_DRDY, 0x00);   /* AMM off = on demand */
    return true;
}

bool HDC2010_Present(void) { return s_dev.bus != NULL; }

bool HDC2010_Trigger(void)
{
    /* 14-bit temp + 14-bit RH, both channels, MEAS_TRIG */
    return I2CDev_WriteByte(&s_dev, REG_MEAS_CONFIG, 0x01);
}

bool HDC2010_Read(int16_t *temp_x10, uint16_t *rh_x10)
{
    uint8_t b[4];
    if (!I2CDev_Read(&s_dev, REG_TEMP_L, b, 4)) return false;

    uint32_t t  = (uint32_t)(b[0] | (b[1] << 8));
    uint32_t rh = (uint32_t)(b[2] | (b[3] << 8));

    /* T = raw/65536*165 - 40,  RH = raw/65536*100 */
    *temp_x10 = (int16_t)((int32_t)((t * 1650UL) >> 16) - 400);
    *rh_x10   = (uint16_t)((rh * 1000UL) >> 16);
    return true;
}
