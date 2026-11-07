#include "i2c_dev.h"
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;

bool I2CDev_Find(i2c_dev_t *dev, const uint8_t *addrs, uint8_t n_addrs)
{
    I2C_HandleTypeDef *buses[2] = { &hi2c1, &hi2c2 };

    dev->bus = NULL;
    for (int b = 0; b < 2; b++) {
        for (uint8_t i = 0; i < n_addrs; i++) {
            if (HAL_I2C_IsDeviceReady(buses[b], (uint16_t)(addrs[i] << 1), 2,
                                      I2C_DEV_TIMEOUT_MS) == HAL_OK) {
                dev->bus  = buses[b];
                dev->addr = addrs[i];
                return true;
            }
        }
    }
    return false;
}

bool I2CDev_Read(const i2c_dev_t *dev, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (!dev->bus) return false;
    return HAL_I2C_Mem_Read(dev->bus, (uint16_t)(dev->addr << 1), reg,
                            I2C_MEMADD_SIZE_8BIT, buf, len, I2C_DEV_TIMEOUT_MS) == HAL_OK;
}

bool I2CDev_Write(const i2c_dev_t *dev, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    if (!dev->bus) return false;
    return HAL_I2C_Mem_Write(dev->bus, (uint16_t)(dev->addr << 1), reg,
                             I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len,
                             I2C_DEV_TIMEOUT_MS) == HAL_OK;
}

bool I2CDev_WriteByte(const i2c_dev_t *dev, uint8_t reg, uint8_t val)
{
    return I2CDev_Write(dev, reg, &val, 1);
}

bool I2CDev_ReadByte(const i2c_dev_t *dev, uint8_t reg, uint8_t *val)
{
    return I2CDev_Read(dev, reg, val, 1);
}

const char *I2CDev_BusName(const i2c_dev_t *dev)
{
    if (dev->bus == &hi2c1) return "I2C1";
    if (dev->bus == &hi2c2) return "I2C2";
    return "--";
}

void I2CDev_ScanPrint(void)
{
    I2C_HandleTypeDef *buses[2] = { &hi2c1, &hi2c2 };
    for (int b = 0; b < 2; b++) {
        printf("\r\nI2C%d:", b + 1);
        for (uint8_t a = 0x08; a < 0x78; a++) {
            if (HAL_I2C_IsDeviceReady(buses[b], (uint16_t)(a << 1), 2, 5) == HAL_OK) {
                printf(" 0x%02X", a);
            }
        }
    }
    printf("\r\n");
    fflush(stdout);
}
