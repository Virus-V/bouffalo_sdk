/**
 * @file aht20.c
 * @brief AHT20 temperature and humidity sensor driver
 */

#include "aht20.h"
#include "sensor_i2c.h"
#include "bflb_mtimer.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#define AHT20_ADDR        0x38

/* AHT20 Commands */
#define AHT20_CMD_INIT    0xBE
#define AHT20_CMD_MEASURE 0xAC
#define AHT20_CMD_SOFT_RESET 0x1A

static int sensor_started = 0;
static int sensor_busy = 0;
static uint32_t measurement_delay = 0;

/**
 * @brief CRC8 calculation for AHT20
 */
static uint8_t aht20_crc(uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 8; j > 0; --j) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief Initialize AHT20 sensor
 */
int aht20_init(void)
{
    uint8_t cmd[3];

    /* Soft reset first */
    cmd[0] = AHT20_CMD_SOFT_RESET;
    sensor_i2c_write(AHT20_ADDR, cmd, 1);
    vTaskDelay(pdMS_TO_TICKS(20));  /* Wait for reset to complete */

    /* Send initialization command - same as Arduino */
    cmd[0] = AHT20_CMD_INIT;
    cmd[1] = 0x08;
    cmd[2] = 0x00;
    sensor_i2c_write(AHT20_ADDR, cmd, 3);

    /* Wait for initialization */
    vTaskDelay(pdMS_TO_TICKS(10));

    printf("[AHT20] Initialized\r\n");

    /* Trigger first measurement - like Arduino setup() */
    aht20_start_measurement();

    return 0;
}

/**
 * @brief Start measurement
 */
void aht20_start_measurement(void)
{
    uint8_t cmd[3] = { AHT20_CMD_MEASURE, 0x33, 0x00 };

    sensor_i2c_write(AHT20_ADDR, cmd, 3);
    measurement_delay = bflb_mtimer_get_time_ms();
    sensor_started = 1;
    sensor_busy = 1;
}

/**
 * @brief Check if sensor is busy
 */
void aht20_check_busy(void)
{
    uint8_t status;

    if (!sensor_started || !sensor_busy) {
        return;
    }

    /* Wait at least 80ms */
    if (bflb_mtimer_get_time_ms() - measurement_delay < 80) {
        return;
    }

    /* Check busy status */
    if (sensor_i2c_read(AHT20_ADDR, &status, 1) == 0) {
        if (!(status & 0x80)) {
            sensor_busy = 0;
        }
    }

    /* Timeout after 200ms */
    if (bflb_mtimer_get_time_ms() - measurement_delay >= 200) {
        sensor_busy = 0;
    }
}

/**
 * @brief Read temperature and humidity data
 */
int aht20_read(float *temperature, float *humidity)
{
    uint8_t data[7];
    uint8_t calc_crc;
    uint32_t start_time;

    /* If measurement was triggered, wait for it to complete */
    if (sensor_started && sensor_busy) {
        start_time = bflb_mtimer_get_time_ms();
        while (sensor_busy) {
            /* Check busy status */
            uint8_t status;
            if (sensor_i2c_read(AHT20_ADDR, &status, 1) == 0) {
                if (!(status & 0x80)) {
                    sensor_busy = 0;
                }
            }
            /* Timeout after 200ms */
            if (bflb_mtimer_get_time_ms() - start_time >= 200) {
                sensor_busy = 0;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (sensor_started && !sensor_busy) {
        /* Request 7 bytes of data */
        if (sensor_i2c_read(AHT20_ADDR, data, 7) != 0) {
            printf("[AHT20] Read failed\r\n");
            return -1;
        }

        /* Check status byte */
        if (data[0] & 0x80) {
            printf("[AHT20] Device busy\r\n");
            return -1;
        }

        /* CRC check */
        calc_crc = aht20_crc(data, 6);
        if (calc_crc != data[6]) {
            printf("[AHT20] CRC failed: calc=0x%02x, expected=0x%02x\r\n", calc_crc, data[6]);
            return -1;
        }

        /* Parse humidity (20 bits): data[1][7:0], data[2][7:0], data[3][7:4] */
        uint32_t humi_raw = (((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((uint32_t)data[3] >> 4)) & 0xFFFFF;

        /* Parse temperature (20 bits): data[3][3:0], data[4][7:0], data[5][7:0] */
        uint32_t temp_raw = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

        /* Convert to physical values using AHT20 formula */
        /* Humidity: (raw / 2^20) * 100% */
        float humi_calc = ((float)humi_raw / 1048576.0f) * 100.0f;

        /* Temperature: ((raw / 2^20) * 200 - 50) °C */
        float temp_calc = (((float)temp_raw / 1048576.0f) * 200.0f) - 50.0f;

        *humidity = humi_calc;
        *temperature = temp_calc;

        /* Trigger next measurement for continuous reading */
        aht20_start_measurement();

        return 0;
    }

    return -1;
}

/**
 * @brief Get AHT20 status
 */
int aht20_is_busy(void)
{
    return sensor_busy;
}

/**
 * @brief Trigger a new measurement and get results
 */
int aht20_measure(float *temperature, float *humidity)
{
    /* Start new measurement */
    aht20_start_measurement();

    /* Wait for measurement to complete */
    for (int i = 0; i < 100; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        aht20_check_busy();
        if (!aht20_is_busy()) {
            break;
        }
    }

    /* Read data */
    return aht20_read(temperature, humidity);
}
