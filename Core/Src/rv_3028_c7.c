/**
 * @file rv3028.c
 * @brief RV-3028-C7 Real-Time Clock (RTC) Driver Implementation
 * 
 * I2C-based RTC module driver for STM32L4 microcontroller.
 * Provides functionality to get and set time using BCD format.
 */

#include "rv_3028_c7.h"

/* ============================================================================ */
/* Private Helper Functions                                                    */
/* ============================================================================ */

/**
 * @brief Convert decimal value to BCD (Binary Coded Decimal)
 * 
 * Example: 25 -> 0x25 (2 in upper nibble, 5 in lower nibble)
 * 
 * @param decimal Decimal value (0-99)
 * @return BCD encoded value
 */
uint8_t rv3028_dec_to_bcd(uint8_t decimal)
{
    return ((decimal / 10) << 4) | (decimal % 10);
}

/**
 * @brief Convert BCD value to decimal
 * 
 * Example: 0x25 -> 25
 * 
 * @param bcd BCD encoded value
 * @return Decimal value
 */
uint8_t rv3028_bcd_to_dec(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/* ============================================================================ */
/* Public API Functions                                                        */
/* ============================================================================ */

/**
 * @brief Initialize RV-3028-C7 RTC driver
 * 
 * Configures the driver and verifies I2C communication with the device.
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param hi2c Pointer to STM32 I2C handle
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_init(rv3028_handle_t *handle, I2C_HandleTypeDef *hi2c)
{
    uint8_t reg_value;
    
    if (handle == NULL || hi2c == NULL) {
        return HAL_ERROR;
    }
    
    handle->hi2c = hi2c;
    
    /* Verify device communication by reading ID register */
    if (rv3028_read_register(handle, RV3028_REG_ID, &reg_value) != HAL_OK) {
        return HAL_ERROR;
    }
    
    return HAL_OK;
}

/**
 * @brief Read a single register from RTC
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param reg Register address
 * @param value Pointer to store register value
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_read_register(rv3028_handle_t *handle, uint8_t reg, uint8_t *value)
{
    return rv3028_read_registers(handle, reg, value, 1);
}

/**
 * @brief Write a single register to RTC
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param reg Register address
 * @param value Value to write
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_write_register(rv3028_handle_t *handle, uint8_t reg, uint8_t value)
{
    return rv3028_write_registers(handle, reg, &value, 1);
}

/**
 * @brief Read multiple registers from RTC
 * 
 * Reads a sequence of registers starting from the specified address.
 * The address pointer automatically increments after each byte.
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param reg Starting register address
 * @param data Pointer to buffer for read data
 * @param length Number of bytes to read
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_read_registers(rv3028_handle_t *handle, uint8_t reg, 
                                        uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status;
    
    if (handle == NULL || handle->hi2c == NULL || data == NULL) {
        return HAL_ERROR;
    }
    
    /* Write register address first (pointer write) */
    status = HAL_I2C_Master_Transmit(handle->hi2c, (RV3028_I2C_ADDR << 1), 
                                      &reg, 1, 100);
    if (status != HAL_OK) {
        return status;
    }
    
    /* Read data from the register address */
    status = HAL_I2C_Master_Receive(handle->hi2c, (RV3028_I2C_ADDR << 1), 
                                     data, length, 100);
    
    return status;
}

/**
 * @brief Write multiple registers to RTC
 * 
 * Writes a sequence of registers starting from the specified address.
 * The address pointer automatically increments after each byte.
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param reg Starting register address
 * @param data Pointer to data buffer
 * @param length Number of bytes to write
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_write_registers(rv3028_handle_t *handle, uint8_t reg, 
                                         const uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status;
    uint8_t buffer[256];
    
    if (handle == NULL || handle->hi2c == NULL || data == NULL) {
        return HAL_ERROR;
    }
    
    if (length > 255) {
        return HAL_ERROR;
    }
    
    /* Prepare buffer: [Register Address | Data] */
    buffer[0] = reg;
    for (int i = 0; i < length; i++) {
        buffer[i + 1] = data[i];
    }
    
    /* Write register address and data */
    status = HAL_I2C_Master_Transmit(handle->hi2c, (RV3028_I2C_ADDR << 1), 
                                      buffer, length + 1, 100);
    
    return status;
}

/**
 * @brief Read current time from RTC
 * 
 * Reads the time and date from RTC registers and converts from BCD to decimal.
 * All values are read in a single I2C transaction for consistency.
 * 
 * Register layout:
 *   0x00: Seconds
 *   0x01: Minutes
 *   0x02: Hours (24h)
 *   0x03: Weekday (1-7)
 *   0x04: Date (1-31)
 *   0x05: Month (1-12)
 *   0x06: Year (0-99)
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param time Pointer to time structure to store the read values
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_get_time(rv3028_handle_t *handle, rv3028_time_t *time)
{
    HAL_StatusTypeDef status;
    uint8_t time_regs[7];  /* Seconds through Year */
    
    if (handle == NULL || handle->hi2c == NULL || time == NULL) {
        return HAL_ERROR;
    }
    
    /* Read all 7 time registers in one transaction (0x00 to 0x06) */
    status = rv3028_read_registers(handle, RV3028_REG_SECONDS, time_regs, 7);
    
    if (status == HAL_OK) {
        /* Convert from BCD to decimal */
        time->seconds = rv3028_bcd_to_dec(time_regs[0]);
        time->minutes = rv3028_bcd_to_dec(time_regs[1]);
        time->hours = rv3028_bcd_to_dec(time_regs[2]);
        time->weekday = rv3028_bcd_to_dec(time_regs[3]);
        time->date = rv3028_bcd_to_dec(time_regs[4]);
        time->month = rv3028_bcd_to_dec(time_regs[5]);
        time->year = rv3028_bcd_to_dec(time_regs[6]);
    }
    
    return status;
}

/**
 * @brief Set time in RTC
 * 
 * Writes the time and date to RTC registers, converting from decimal to BCD.
 * All values are written in a single I2C transaction for consistency.
 * 
 * Register layout:
 *   0x00: Seconds
 *   0x01: Minutes
 *   0x02: Hours (24h)
 *   0x03: Weekday (1-7)
 *   0x04: Date (1-31)
 *   0x05: Month (1-12)
 *   0x06: Year (0-99)
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param time Pointer to time structure with values to set
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_set_time(rv3028_handle_t *handle, const rv3028_time_t *time)
{
    HAL_StatusTypeDef status;
    uint8_t time_regs[7];  /* Seconds through Year */
    
    if (handle == NULL || handle->hi2c == NULL || time == NULL) {
        return HAL_ERROR;
    }
    
    /* Validate input ranges */
    if (time->seconds > 59 || time->minutes > 59 || time->hours > 23 ||
        time->weekday < 1 || time->weekday > 7 || time->date < 1 || 
        time->date > 31 || time->month < 1 || time->month > 12) {
        return HAL_ERROR;
    }
    
    /* Convert from decimal to BCD */
    time_regs[0] = rv3028_dec_to_bcd(time->seconds);
    time_regs[1] = rv3028_dec_to_bcd(time->minutes);
    time_regs[2] = rv3028_dec_to_bcd(time->hours);
    time_regs[3] = rv3028_dec_to_bcd(time->weekday);
    time_regs[4] = rv3028_dec_to_bcd(time->date);
    time_regs[5] = rv3028_dec_to_bcd(time->month);
    time_regs[6] = rv3028_dec_to_bcd(time->year);
    
    /* Write all 7 time registers in one transaction (0x00 to 0x06) */
    status = rv3028_write_registers(handle, RV3028_REG_SECONDS, time_regs, 7);
    
    return status;
}