/**
 * @file bmp280.c
 * @brief BMP280 barometric pressure sensor driver
 */

#include "bmp280.h"
#include "sensor_i2c.h"
#include <stdio.h>
#include <stddef.h>

#define BMP280_ADDR       0x77

/* BMP280 Registers */
#define BMP280_REG_CHIP_ID    0xD0
#define BMP280_REG_RESET      0xE0
#define BMP280_REG_STATUS     0xF3
#define BMP280_REG_CTRL_MEAS  0xF4
#define BMP280_REG_CONFIG     0xF5
#define BMP280_REG_TEMP_XLSB  0xFC
#define BMP280_REG_TEMP_LSB   0xFB
#define BMP280_REG_TEMP_MSB   0xFA
#define BMP280_REG_PRESS_XLSB 0xF9
#define BMP280_REG_PRESS_LSB  0xF8
#define BMP280_REG_PRESS_MSB  0xF7
#define BMP280_REG_DIG_T1     0x88

/* Compensation parameters */
static uint16_t dig_T1;
static int16_t dig_T2;
static int16_t dig_T3;
static uint16_t dig_P1;
static int16_t dig_P2;
static int16_t dig_P3;
static int16_t dig_P4;
static int16_t dig_P5;
static int16_t dig_P6;
static int16_t dig_P7;
static int16_t dig_P8;
static int16_t dig_P9;

static int32_t t_fine;
static uint8_t bmp280_initialized = 0;
static uint8_t bmp280_addr = 0;

/**
 * @brief Read compensation parameter
 */
static void bmp280_read_compensation_params(void)
{
    uint8_t data[24];
    uint8_t reg = BMP280_REG_DIG_T1;

    sensor_i2c_write_read(bmp280_addr, reg, data, 24);

    dig_T1 = (uint16_t)(data[1] << 8) | data[0];
    dig_T2 = (int16_t)(data[3] << 8) | data[2];
    dig_T3 = (int16_t)(data[5] << 8) | data[4];
    dig_P1 = (uint16_t)(data[7] << 8) | data[6];
    dig_P2 = (int16_t)(data[9] << 8) | data[8];
    dig_P3 = (int16_t)(data[11] << 8) | data[10];
    dig_P4 = (int16_t)(data[13] << 8) | data[12];
    dig_P5 = (int16_t)(data[15] << 8) | data[14];
    dig_P6 = (int16_t)(data[17] << 8) | data[16];
    dig_P7 = (int16_t)(data[19] << 8) | data[18];
    dig_P8 = (int16_t)(data[21] << 8) | data[20];
    dig_P9 = (int16_t)(data[23] << 8) | data[22];
}

/**
 * @brief Initialize BMP280 sensor
 */
int bmp280_init(void)
{
    uint8_t chip_id;
    uint8_t config[2];
    uint8_t addr;
    uint8_t found = 0;

    /* Try possible BMP280 addresses: 0x77 and 0x76 */
    for (int i = 0; i < 2; i++) {
        addr = (i == 0) ? 0x77 : 0x76;
        /* Read chip ID - must write register address first */
        int ret = sensor_i2c_write_read(addr, BMP280_REG_CHIP_ID, &chip_id, 1);
        printf("[BMP280] Try addr 0x%02X, reg 0x%02X, ret=%d, id=0x%02X\r\n", addr, BMP280_REG_CHIP_ID, ret, chip_id);
        if (chip_id == 0x58) {
            bmp280_addr = addr;
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("[BMP280] Sensor not found at 0x77 or 0x76\r\n");
        return -1;
    }

    printf("[BMP280] Found at address 0x%02x, Chip ID: 0x%02x\r\n", bmp280_addr, chip_id);

    /* Soft reset */
    config[0] = BMP280_REG_RESET;
    config[1] = 0xB6;
    sensor_i2c_write(bmp280_addr, config, 2);

    /* Wait for NVM copy to complete */
    for (int i = 0; i < 100; i++) {
        sensor_i2c_write_read(bmp280_addr, BMP280_REG_STATUS, config, 1);
        if (!(config[0] & 0x01)) {
            break;
        }
    }

    /* Read compensation parameters */
    bmp280_read_compensation_params();

    /* Set sleep mode */
    config[0] = BMP280_REG_CTRL_MEAS;
    config[1] = 0x00;
    sensor_i2c_write(bmp280_addr, config, 2);

    /* Configure: temp oversampling x1, press oversampling x1, normal mode */
    config[0] = BMP280_REG_CTRL_MEAS;
    config[1] = (0x01 << 5) | (0x01 << 2) | 0x03;
    sensor_i2c_write(bmp280_addr, config, 2);

    /* Configure: standby 125ms, filter x4 */
    config[0] = BMP280_REG_CONFIG;
    config[1] = (0x02 << 5) | (0x04 << 2);
    sensor_i2c_write(bmp280_addr, config, 2);

    printf("[BMP280] Initialized\r\n");
    bmp280_initialized = 1;
    return 0;
}

/**
 * @brief Read temperature (returns t_fine for pressure calculation)
 */
int bmp280_read_temperature(float *temperature)
{
    uint8_t data[3];
    int32_t var1, var2, adc_T;

    /* Read temperature registers */
    data[0] = BMP280_REG_TEMP_MSB;
    sensor_i2c_write_read(bmp280_addr, BMP280_REG_TEMP_MSB, data, 3);

    adc_T = ((int32_t)data[0] << 16) | ((int32_t)data[1] << 8) | data[2];
    adc_T >>= 4;

    /* Compensation formula from datasheet */
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;

    t_fine = var1 + var2;

    if (temperature != NULL) {
        *temperature = (((t_fine * 5) + 128) >> 8) / 100.0f;
    }

    return 0;
}

/**
 * @brief Read pressure
 */
int bmp280_read_pressure(float *pressure)
{
    uint8_t data[3];
    int64_t var1, var2, p;
    int32_t adc_P;

    /* First read temperature to get t_fine */
    bmp280_read_temperature(NULL);

    /* Read pressure registers */
    sensor_i2c_write_read(bmp280_addr, BMP280_REG_PRESS_MSB, data, 3);

    adc_P = ((int32_t)data[0] << 16) | ((int32_t)data[1] << 8) | data[2];
    adc_P >>= 4;

    /* Compensation formula from datasheet */
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 + (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;

    if (var1 == 0) {
        return -1;
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);

    if (pressure != NULL) {
        *pressure = p / 256.0f / 100.0f;  /* Convert to hPa */
    }

    return 0;
}

/**
 * @brief Read both temperature and pressure
 */
int bmp280_read(float *temperature, float *pressure)
{
    int ret;

    if (!bmp280_initialized) {
        return -1;
    }

    ret = bmp280_read_temperature(temperature);
    if (ret != 0) {
        return ret;
    }

    ret = bmp280_read_pressure(pressure);

    return ret;
}
