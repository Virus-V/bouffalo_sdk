/**
 * @file sensor_i2c.h
 * @brief I2C driver header for sensors
 */

#ifndef __SENSOR_I2C_H
#define __SENSOR_I2C_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Scan I2C bus for devices
 */
void sensor_i2c_scan(void);

/**
 * @brief Initialize I2C with custom GPIO pins
 * @param scl_gpio SCL GPIO pin number (e.g., GPIO_PIN_10)
 * @param sda_gpio SDA GPIO pin number (e.g., GPIO_PIN_11)
 */
void sensor_i2c_init(uint8_t scl_gpio, uint8_t sda_gpio);

/**
 * @brief Write data to I2C device
 * @param addr I2C device address (7-bit)
 * @param data pointer to data buffer
 * @param len number of bytes to write
 * @return 0 on success, negative on error
 */
int sensor_i2c_write(uint8_t addr, uint8_t *data, uint16_t len);

/**
 * @brief Read data from I2C device
 * @param addr I2C device address (7-bit)
 * @param data pointer to data buffer
 * @param len number of bytes to read
 * @return 0 on success, negative on error
 */
int sensor_i2c_read(uint8_t addr, uint8_t *data, uint16_t len);

/**
 * @brief Write register and read data (I2C write-then-read)
 * @param addr I2C device address (7-bit)
 * @param reg register address to write
 * @param rxbuf pointer to read buffer
 * @param rxlen number of bytes to read
 * @return 0 on success, negative on error
 */
int sensor_i2c_write_read(uint8_t addr, uint8_t reg, uint8_t *rxbuf, uint16_t rxlen);

#endif /* __SENSOR_I2C_H */
