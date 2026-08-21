#include "mpu6050.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "MPU6050";

#define MPU6050_REG_SELF_TEST_X       0x0D
#define MPU6050_REG_SELF_TEST_Y       0x0E
#define MPU6050_REG_SELF_TEST_Z       0x0F
#define MPU6050_REG_SELF_TEST_A       0x10
#define MPU6050_REG_SMPLRT_DIV        0x19
#define MPU6050_REG_CONFIG            0x1A
#define MPU6050_REG_GYRO_CONFIG       0x1B
#define MPU6050_REG_ACCEL_CONFIG      0x1C
#define MPU6050_REG_FIFO_EN           0x23
#define MPU6050_REG_I2C_MST_CTRL      0x24
#define MPU6050_REG_I2C_SLV0_ADDR     0x25
#define MPU6050_REG_I2C_SLV0_REG      0x26
#define MPU6050_REG_I2C_SLV0_CTRL     0x27
#define MPU6050_REG_I2C_SLV1_ADDR     0x28
#define MPU6050_REG_I2C_SLV1_REG      0x29
#define MPU6050_REG_I2C_SLV1_CTRL     0x2A
#define MPU6050_REG_I2C_SLV2_ADDR     0x2B
#define MPU6050_REG_I2C_SLV2_REG      0x2C
#define MPU6050_REG_I2C_SLV2_CTRL     0x2D
#define MPU6050_REG_I2C_SLV3_ADDR     0x2E
#define MPU6050_REG_I2C_SLV3_REG      0x2F
#define MPU6050_REG_I2C_SLV3_CTRL     0x30
#define MPU6050_REG_I2C_SLV4_ADDR     0x31
#define MPU6050_REG_I2C_SLV4_REG      0x32
#define MPU6050_REG_I2C_SLV4_DO       0x33
#define MPU6050_REG_I2C_SLV4_CTRL     0x34
#define MPU6050_REG_I2C_SLV4_DI       0x35
#define MPU6050_REG_I2C_MST_STATUS    0x36
#define MPU6050_REG_INT_PIN_CFG       0x37
#define MPU6050_REG_INT_ENABLE        0x38
#define MPU6050_REG_INT_STATUS        0x3A
#define MPU6050_REG_ACCEL_XOUT_H      0x3B
#define MPU6050_REG_ACCEL_XOUT_L      0x3C
#define MPU6050_REG_ACCEL_YOUT_H      0x3D
#define MPU6050_REG_ACCEL_YOUT_L      0x3E
#define MPU6050_REG_ACCEL_ZOUT_H      0x3F
#define MPU6050_REG_ACCEL_ZOUT_L      0x40
#define MPU6050_REG_TEMP_OUT_H        0x41
#define MPU6050_REG_TEMP_OUT_L        0x42
#define MPU6050_REG_GYRO_XOUT_H       0x43
#define MPU6050_REG_GYRO_XOUT_L       0x44
#define MPU6050_REG_GYRO_YOUT_H       0x45
#define MPU6050_REG_GYRO_YOUT_L       0x46
#define MPU6050_REG_GYRO_ZOUT_H       0x47
#define MPU6050_REG_GYRO_ZOUT_L       0x48
#define MPU6050_REG_USER_CTRL         0x6A
#define MPU6050_REG_PWR_MGMT_1        0x6B
#define MPU6050_REG_PWR_MGMT_2        0x6C
#define MPU6050_REG_WHO_AM_I          0x75

typedef struct {
    i2c_port_t i2c_port;
    uint8_t address;
} mpu6050_dev_t;

static esp_err_t mpu6050_write_reg(mpu6050_handle_t dev, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    return i2c_master_write_to_device(dev->i2c_port, dev->address, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t mpu6050_read_reg(mpu6050_handle_t dev, uint8_t reg, uint8_t *data) {
    return i2c_master_write_read_device(dev->i2c_port, dev->address, &reg, 1, data, 1, pdMS_TO_TICKS(100));
}

static esp_err_t mpu6050_read_regs(mpu6050_handle_t dev, uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(dev->i2c_port, dev->address, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

mpu6050_handle_t mpu6050_create(i2c_port_t i2c_port, uint8_t address) {
    mpu6050_dev_t *dev = (mpu6050_dev_t *)malloc(sizeof(mpu6050_dev_t));
    if (!dev) return NULL;
    dev->i2c_port = i2c_port;
    dev->address = address;
    return dev;
}

void mpu6050_delete(mpu6050_handle_t dev) {
    if (dev) free(dev);
}

esp_err_t mpu6050_init(mpu6050_handle_t dev) {
    if (!dev) return ESP_ERR_INVALID_ARG;

    uint8_t who_am_i = 0;
    esp_err_t ret = mpu6050_read_reg(dev, MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) return ret;
    if (who_am_i != 0x68) {
        ESP_LOGE(TAG, "WHO_AM_I = 0x%02X, expected 0x68", who_am_i);
        return ESP_ERR_NOT_FOUND;
    }

    ret = mpu6050_write_reg(dev, MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_SMPLRT_DIV, 0x07);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_CONFIG, 0x06);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_GYRO_CONFIG, 0x18);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_ACCEL_CONFIG, 0x18);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_INT_PIN_CFG, 0x02);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_INT_ENABLE, 0x01);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_USER_CTRL, 0x00);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_FIFO_EN, 0x00);
    if (ret != ESP_OK) return ret;

    ret = mpu6050_write_reg(dev, MPU6050_REG_I2C_MST_CTRL, 0x00);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

bool mpu6050_test_connection(mpu6050_handle_t dev) {
    if (!dev) return false;
    uint8_t who_am_i = 0;
    esp_err_t ret = mpu6050_read_reg(dev, MPU6050_REG_WHO_AM_I, &who_am_i);
    return (ret == ESP_OK && who_am_i == 0x68);
}

esp_err_t mpu6050_get_motion6(mpu6050_handle_t dev, int16_t *ax, int16_t *ay, int16_t *az, int16_t *gx, int16_t *gy, int16_t *gz) {
    if (!dev || !ax || !ay || !az || !gx || !gy || !gz) return ESP_ERR_INVALID_ARG;

    uint8_t data[14];
    esp_err_t ret = mpu6050_read_regs(dev, MPU6050_REG_ACCEL_XOUT_H, data, 14);
    if (ret != ESP_OK) return ret;

    *ax = (int16_t)((data[0] << 8) | data[1]);
    *ay = (int16_t)((data[2] << 8) | data[3]);
    *az = (int16_t)((data[4] << 8) | data[5]);
    *gx = (int16_t)((data[8] << 8) | data[9]);
    *gy = (int16_t)((data[10] << 8) | data[11]);
    *gz = (int16_t)((data[12] << 8) | data[13]);

    return ESP_OK;
}

esp_err_t mpu6050_set_gyro_range(mpu6050_handle_t dev, mpu6050_gyro_fs_t range) {
    if (!dev) return ESP_ERR_INVALID_ARG;
    uint8_t value = (range & 0x03) << 3;
    return mpu6050_write_reg(dev, MPU6050_REG_GYRO_CONFIG, value);
}

esp_err_t mpu6050_set_accel_range(mpu6050_handle_t dev, mpu6050_accel_fs_t range) {
    if (!dev) return ESP_ERR_INVALID_ARG;
    uint8_t value = (range & 0x03) << 3;
    return mpu6050_write_reg(dev, MPU6050_REG_ACCEL_CONFIG, value);
}

esp_err_t mpu6050_set_sleep(mpu6050_handle_t dev, bool enabled) {
    if (!dev) return ESP_ERR_INVALID_ARG;
    uint8_t reg = 0;
    esp_err_t ret = mpu6050_read_reg(dev, MPU6050_REG_PWR_MGMT_1, &reg);
    if (ret != ESP_OK) return ret;
    if (enabled) {
        reg |= 0x40;
    } else {
        reg &= ~0x40;
    }
    return mpu6050_write_reg(dev, MPU6050_REG_PWR_MGMT_1, reg);
}