/**
 * @file aht20.h
 * @brief AHT20 temperature and humidity sensor driver header
 */

#ifndef __AHT20_H
#define __AHT20_H

#include <stdint.h>

/**
 * @brief Initialize AHT20 sensor
 * @return 0 on success, negative on error
 */
int aht20_init(void);

/**
 * @brief Start measurement (trigger)
 */
void aht20_start_measurement(void);

/**
 * @brief Read temperature and humidity data (after measurement)
 * @param temperature pointer to store temperature value (Celsius)
 * @param humidity pointer to store humidity value (percentage)
 * @return 0 on success, negative on error
 */
int aht20_read(float *temperature, float *humidity);

/**
 * @brief Trigger a new measurement and get results
 * @param temperature pointer to store temperature value (Celsius)
 * @param humidity pointer to store humidity value (percentage)
 * @return 0 on success, negative on error
 */
int aht20_measure(float *temperature, float *humidity);

/**
 * @brief Check if sensor is busy
 * @return 1 if busy, 0 if not
 */
int aht20_is_busy(void);

/**
 * @brief Check and update busy status
 */
void aht20_check_busy(void);

#endif /* __AHT20_H */
