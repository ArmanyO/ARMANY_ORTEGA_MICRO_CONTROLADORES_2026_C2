#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mpu6050_dev *mpu6050_handle_t;

#define MPU6050_ADDR_DEFAULT        0x68
#define MPU6050_ADDR_AD0_HIGH       0x69

typedef enum {
    MPU6050_GYRO_FS_250  = 0,
    MPU6050_GYRO_FS_500  = 1,
    MPU6050_GYRO_FS_1000 = 2,
    MPU6050_GYRO_FS_2000 = 3,
} mpu6050_gyro_fs_t;

typedef enum {
    MPU6050_ACCEL_FS_2  = 0,
    MPU6050_ACCEL_FS_4  = 1,
    MPU6050_ACCEL_FS_8  = 2,
    MPU6050_ACCEL_FS_16 = 3,
} mpu6050_accel_fs_t;

mpu6050_handle_t mpu6050_create(i2c_port_t i2c_port, uint8_t address);
void mpu6050_delete(mpu6050_handle_t dev);
esp_err_t mpu6050_init(mpu6050_handle_t dev);
bool mpu6050_test_connection(mpu6050_handle_t dev);
esp_err_t mpu6050_get_motion6(mpu6050_handle_t dev, int16_t *ax, int16_t *ay, int16_t *az, int16_t *gx, int16_t *gy, int16_t *gz);
esp_err_t mpu6050_set_gyro_range(mpu6050_handle_t dev, mpu6050_gyro_fs_t range);
esp_err_t mpu6050_set_accel_range(mpu6050_handle_t dev, mpu6050_accel_fs_t range);
esp_err_t mpu6050_set_sleep(mpu6050_handle_t dev, bool enabled);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_H