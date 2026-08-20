#pragma once

#include <stdint.h>
#include "config.h"
#include "esp_err.h"

typedef enum {
    MOTOR_STOP = 0,
    MOTOR_OPEN,
    MOTOR_CLOSE
} motor_direction_t;

esp_err_t motor_control_init(const app_config_t *cfg);
void motor_control_apply_config(const app_config_t *cfg);
void motor_control_drive(motor_direction_t direction, uint32_t duty_percent);
void motor_control_stop(void);
motor_direction_t motor_control_get_direction(void);
