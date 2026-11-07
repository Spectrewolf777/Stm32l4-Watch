/**
  * @file    i2c_dev.h
  * @brief   Tiny helper shared by the sensor drivers. A device is a
  *          (bus, 7-bit address) pair; I2CDev_Find() probes both I2C1 and
  *          I2C2 so drivers don't need to know which bus a chip is wired to.
  */
#ifndef I2C_DEV_H
#define I2C_DEV_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#define I2C_DEV_TIMEOUT_MS  20U

typedef struct {
    I2C_HandleTypeDef *bus;   /* NULL = not found */
    uint8_t            addr;  /* 7-bit */
} i2c_dev_t;

/* Probe each candidate address on I2C1 then I2C2. First ACK wins. */
bool I2CDev_Find(i2c_dev_t *dev, const uint8_t *addrs, uint8_t n_addrs);

bool I2CDev_Read(const i2c_dev_t *dev, uint8_t reg, uint8_t *buf, uint16_t len);
bool I2CDev_Write(const i2c_dev_t *dev, uint8_t reg, const uint8_t *buf, uint16_t len);
bool I2CDev_WriteByte(const i2c_dev_t *dev, uint8_t reg, uint8_t val);
bool I2CDev_ReadByte(const i2c_dev_t *dev, uint8_t reg, uint8_t *val);

/* "I2C1" / "I2C2" / "--" for printing */
const char *I2CDev_BusName(const i2c_dev_t *dev);

/* Print every ACKing address on both buses over USB CDC */
void I2CDev_ScanPrint(void);

#endif /* I2C_DEV_H */
