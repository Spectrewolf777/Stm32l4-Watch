#ifndef RV_3028_C7_H
#define RV_3028_C7_H

#include "stm32L4xx_hal.h" // CHANGE THIS to your specific STM32 family header
#include <stdint.h>
#include <stdbool.h>


/* RV-3028-C7 I2C Slave Address                                               */
/* ============================================================================ */
#define RV3028_I2C_ADDR         0x52    /**< I2C Slave Address (7-bit) */
#define RV3028_I2C_ADDR_WRITE   0xA4    /**< I2C Write Address (8-bit with R/W=0) */
#define RV3028_I2C_ADDR_READ    0xA5    /**< I2C Read Address (8-bit with R/W=1) */
 
/* ============================================================================ */
/* RV-3028-C7 Register Map                                                     */
/* ============================================================================ */
 
/* Clock & Calendar Registers */
#define RV3028_REG_SECONDS      0x00    /**< Seconds register (00-59) */
#define RV3028_REG_MINUTES      0x01    /**< Minutes register (00-59) */
#define RV3028_REG_HOURS        0x02    /**< Hours register 24h (00-23) */
#define RV3028_REG_WEEKDAY      0x03    /**< Weekday register (1=Monday, 7=Sunday) */
#define RV3028_REG_DATE         0x04    /**< Date register (01-31) */
#define RV3028_REG_MONTH        0x05    /**< Month register (01-12) */
#define RV3028_REG_YEAR         0x06    /**< Year register (00-99, 2000-2099) */
 
/* Alarm Registers */
#define RV3028_REG_MINUTES_ALARM 0x07   /**< Minutes Alarm register */
#define RV3028_REG_HOURS_ALARM   0x08   /**< Hours Alarm register */
#define RV3028_REG_WEEKDAY_ALARM 0x09   /**< Weekday/Date Alarm register */
 
/* Timer Registers */
#define RV3028_REG_TIMER_VALUE0 0x0A    /**< Timer Value 0 */
#define RV3028_REG_TIMER_VALUE1 0x0B    /**< Timer Value 1 */
#define RV3028_REG_TIMER_STATUS0 0x0C   /**< Timer Status 0 */
#define RV3028_REG_TIMER_STATUS1 0x0D   /**< Timer Status 1 */
 
/* Status & Control Registers */
#define RV3028_REG_STATUS       0x0E    /**< Status register */
#define RV3028_REG_CONTROL1     0x0F    /**< Control 1 register */
#define RV3028_REG_CONTROL2     0x10    /**< Control 2 register */
#define RV3028_REG_GPBITS       0x11    /**< GP Bits register */
#define RV3028_REG_CLKINT_MASK  0x12    /**< Clock Interrupt Mask register */
#define RV3028_REG_EVENT_CTRL   0x13    /**< Event Control register */
 
/* Timestamp Registers */
#define RV3028_REG_COUNT_TS     0x14    /**< Count Timestamp */
#define RV3028_REG_SECONDS_TS   0x15    /**< Seconds Timestamp */
#define RV3028_REG_MINUTES_TS   0x16    /**< Minutes Timestamp */
#define RV3028_REG_HOURS_TS     0x17    /**< Hours Timestamp 24h */
#define RV3028_REG_DATE_TS      0x18    /**< Date Timestamp */
#define RV3028_REG_MONTH_TS     0x19    /**< Month Timestamp */
#define RV3028_REG_YEAR_TS      0x1A    /**< Year Timestamp */
 
/* UNIX Time Registers */
#define RV3028_REG_UNIX_TIME0   0x1B    /**< UNIX Time 0 (LSB) */
#define RV3028_REG_UNIX_TIME1   0x1C    /**< UNIX Time 1 */
#define RV3028_REG_UNIX_TIME2   0x1D    /**< UNIX Time 2 */
#define RV3028_REG_UNIX_TIME3   0x1E    /**< UNIX Time 3 (MSB) */
 
/* User Memory */
#define RV3028_REG_USER_RAM1    0x1F    /**< User RAM 1 */
#define RV3028_REG_USER_RAM2    0x20    /**< User RAM 2 */
 
/* Password & EEPROM Registers */
#define RV3028_REG_PASSWORD0    0x21    /**< Password register 0 */
#define RV3028_REG_PASSWORD1    0x22    /**< Password register 1 */
#define RV3028_REG_PASSWORD2    0x23    /**< Password register 2 */
#define RV3028_REG_PASSWORD3    0x24    /**< Password register 3 */
#define RV3028_REG_EEADDRESS    0x25    /**< EEPROM Address register */
#define RV3028_REG_EEDATA       0x26    /**< EEPROM Data register */
#define RV3028_REG_EECMD        0x27    /**< EEPROM Command register */
#define RV3028_REG_ID           0x28    /**< ID register */
 
/* Configuration EEPROM with RAM mirror (0x30-0x37) */
#define RV3028_REG_EEPROM_CLKOUT 0x35   /**< EEPROM Clock Output configuration */
#define RV3028_REG_EEPROM_OFFSET 0x36   /**< EEPROM Frequency Offset */
#define RV3028_REG_EEPROM_BACKUP 0x37   /**< EEPROM Backup register */
 
/* ============================================================================ */
/* Bit Definitions                                                             */
/* ============================================================================ */
 
/* Status Register (0x0E) */
#define RV3028_STATUS_PORF      (1 << 0)    /**< Power On Reset Flag */
#define RV3028_STATUS_EVF       (1 << 1)    /**< External Event Flag */
#define RV3028_STATUS_BSF       (1 << 2)    /**< Backup Switchover Flag */
#define RV3028_STATUS_TF        (1 << 3)    /**< Periodic Countdown Timer Flag */
#define RV3028_STATUS_AF        (1 << 4)    /**< Alarm Flag */
#define RV3028_STATUS_UF        (1 << 5)    /**< Periodic Time Update Flag */
#define RV3028_STATUS_EEBUSY    (1 << 7)    /**< EEPROM Busy Flag */
 
/* Control 1 Register (0x0F) */
#define RV3028_CTRL1_TRPT       (1 << 1)    /**< Timer Repeat */
#define RV3028_CTRL1_WUTE       (1 << 2)    /**< Periodic Time Update Interrupt Enable */
#define RV3028_CTRL1_TE         (1 << 3)    /**< Timer Enable */
#define RV3028_CTRL1_SYSRESET   (1 << 4)    /**< System Reset */
 
/* Control 2 Register (0x10) */
#define RV3028_CTRL2_AIE        (1 << 0)    /**< Alarm Interrupt Enable */
#define RV3028_CTRL2_EVIE       (1 << 2)    /**< External Event Interrupt Enable */
#define RV3028_CTRL2_BSIE       (1 << 3)    /**< Backup Switchover Interrupt Enable */
#define RV3028_CTRL2_TIE        (1 << 4)    /**< Timer Interrupt Enable */
#define RV3028_CTRL2_PORIE      (1 << 7)    /**< Power On Reset Interrupt Enable */
 
/* ============================================================================ */
/* Data Structures                                                             */
/* ============================================================================ */
 
/**
 * @struct rv3028_time
 * @brief Time structure for RV-3028-C7
 * 
 * All values are in decimal format (not BCD).
 * Year is 00-99 representing 2000-2099.
 */
typedef struct {
    uint8_t seconds;        /**< Seconds (0-59) */
    uint8_t minutes;        /**< Minutes (0-59) */
    uint8_t hours;          /**< Hours (0-23) */
    uint8_t weekday;        /**< Weekday (1=Monday, 7=Sunday) */
    uint8_t date;           /**< Date/Day (1-31) */
    uint8_t month;          /**< Month (1-12) */
    uint8_t year;           /**< Year (0-99 for 2000-2099) */
} rv3028_time_t;
 
/**
 * @struct rv3028_handle
 * @brief RV-3028-C7 Device Handle
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;    /**< Pointer to I2C handle */
} rv3028_handle_t;
 
/* ============================================================================ */
/* Function Prototypes                                                         */
/* ============================================================================ */
 
/**
 * @brief Initialize RV-3028-C7 RTC driver
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param hi2c Pointer to STM32 I2C handle
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_init(rv3028_handle_t *handle, I2C_HandleTypeDef *hi2c);
 
/**
 * @brief Read current time from RTC
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param time Pointer to time structure to store the read values
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_get_time(rv3028_handle_t *handle, rv3028_time_t *time);
 
/**
 * @brief Set time in RTC
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param time Pointer to time structure with values to set
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_set_time(rv3028_handle_t *handle, const rv3028_time_t *time);
 
/**
 * @brief Read a single register from RTC
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param reg Register address
 * @param value Pointer to store register value
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_read_register(rv3028_handle_t *handle, uint8_t reg, uint8_t *value);
 
/**
 * @brief Write a single register to RTC
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param reg Register address
 * @param value Value to write
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_write_register(rv3028_handle_t *handle, uint8_t reg, uint8_t value);
 
/**
 * @brief Read multiple registers from RTC
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param reg Starting register address
 * @param data Pointer to buffer for read data
 * @param length Number of bytes to read
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_read_registers(rv3028_handle_t *handle, uint8_t reg, 
                                        uint8_t *data, uint16_t length);
 
/**
 * @brief Write multiple registers to RTC
 * 
 * @param handle Pointer to RV-3028-C7 handle
 * @param reg Starting register address
 * @param data Pointer to data buffer
 * @param length Number of bytes to write
 * @return HAL_OK if successful, otherwise HAL error code
 */
HAL_StatusTypeDef rv3028_write_registers(rv3028_handle_t *handle, uint8_t reg, 
                                         const uint8_t *data, uint16_t length);
 
/**
 * @brief Convert decimal value to BCD (Binary Coded Decimal)
 * 
 * @param decimal Decimal value (0-99)
 * @return BCD encoded value
 */
uint8_t rv3028_dec_to_bcd(uint8_t decimal);
 
/**
 * @brief Convert BCD value to decimal
 * 
 * @param bcd BCD encoded value
 * @return Decimal value
 */
uint8_t rv3028_bcd_to_dec(uint8_t bcd);


#endif /* RV_3028_C7_H */