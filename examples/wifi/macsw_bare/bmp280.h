/**
 * @file bmp280.h
 * @brief BMP280 barometric pressure sensor driver header
 */

#ifndef __BMP280_H
#define __BMP280_H

#include <stdint.h>

/**
 * @brief Initialize BMP280 sensor
 * @return 0 on success, negative on error
 */
int bmp280_init(void);

/**
 * @brief Read temperature and pressure
 * @param temperature pointer to store temperature value (Celsius)
 * @param pressure pointer to store pressure value (hPa)
 * @return 0 on success, negative on error
 */
int bmp280_read(float *temperature, float *pressure);

/**
 * @brief Read temperature only
 * @param temperature pointer to store temperature value (Celsius)
 * @return 0 on success, negative on error
 */
int bmp280_read_temperature(float *temperature);

/**
 * @brief Read pressure only
 * @param pressure pointer to store pressure value (hPa)
 * @return 0 on success, negative on error
 */
int bmp280_read_pressure(float *pressure);

#endif /* __BMP280_H */
