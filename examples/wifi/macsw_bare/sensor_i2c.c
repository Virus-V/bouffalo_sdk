/**
 * @file sensor_i2c.c
 * @brief I2C driver for sensors (AHT20 and BMP280)
 */

#include "sensor_i2c.h"
#include "bflb_i2c.h"
#include "bflb_gpio.h"
#include "bflb_mtimer.h"
#include <stdio.h>
#include <stddef.h>

static struct bflb_device_s *i2c0 = NULL;

/**
 * @brief Scan I2C bus for devices
 */
void sensor_i2c_scan(void)
{
    uint8_t addr;
    int ret;

    printf("[I2C] Scanning bus...\r\n");
    for (addr = 1; addr < 128; addr++) {
        struct bflb_i2c_msg_s msg;
        uint8_t data;

        msg.addr = addr;
        msg.flags = I2C_M_WRITE;
        msg.buffer = &data;
        msg.length = 1;

        ret = bflb_i2c_transfer(i2c0, &msg, 1);
        if (ret == 0) {
            printf("[I2C] Found device at 0x%02X\r\n", addr);
        }
    }
    printf("[I2C] Scan complete\r\n");
}

/**
 * @brief Initialize I2C0 with custom GPIO configuration
 * @param scl_gpio SCL GPIO pin number
 * @param sda_gpio SDA GPIO pin number
 */
void sensor_i2c_init(uint8_t scl_gpio, uint8_t sda_gpio)
{
    struct bflb_device_s *gpio;

    gpio = bflb_device_get_by_name("gpio");

    /* Configure SDA pin */
    bflb_gpio_init(gpio, sda_gpio, GPIO_FUNC_I2C0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
    /* Configure SCL pin */
    bflb_gpio_init(gpio, scl_gpio, GPIO_FUNC_I2C0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);

    i2c0 = bflb_device_get_by_name("i2c0");
    bflb_i2c_init(i2c0, 100000);  /* 100kHz for sensors */
}

/**
 * @brief Write data to I2C device
 */
int sensor_i2c_write(uint8_t addr, uint8_t *data, uint16_t len)
{
    struct bflb_i2c_msg_s msgs[1];

    msgs[0].addr = addr;
    msgs[0].flags = I2C_M_WRITE;
    msgs[0].buffer = data;
    msgs[0].length = len;

    return bflb_i2c_transfer(i2c0, msgs, 1);
}

/**
 * @brief Read data from I2C device
 */
int sensor_i2c_read(uint8_t addr, uint8_t *data, uint16_t len)
{
    struct bflb_i2c_msg_s msgs[1];

    msgs[0].addr = addr;
    msgs[0].flags = I2C_M_READ;
    msgs[0].buffer = data;
    msgs[0].length = len;

    return bflb_i2c_transfer(i2c0, msgs, 1);
}

/**
 * @brief Write register and read data (I2C write-then-read)
 */
int sensor_i2c_write_read(uint8_t addr, uint8_t reg, uint8_t *rxbuf, uint16_t rxlen)
{
    struct bflb_i2c_msg_s msgs[2];

    /* Write register address */
    msgs[0].addr = addr;
    msgs[0].flags = I2C_M_WRITE;
    msgs[0].buffer = &reg;
    msgs[0].length = 1;

    /* Read data */
    msgs[1].addr = addr;
    msgs[1].flags = I2C_M_READ;
    msgs[1].buffer = rxbuf;
    msgs[1].length = rxlen;

    return bflb_i2c_transfer(i2c0, msgs, 2);
}
