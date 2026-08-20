#pragma once

#include <stdbool.h>
#include <stdint.h>

#define APP_DEVICE_ID "porton_esp32s3_01"

#define APP_WIFI_SSID "Las Penas"
#define APP_WIFI_PASSWORD "Pena123321"
#define APP_MQTT_BROKER_URI "mqtt://broker.emqx.io:1883"

// Pines iniciales para ESP32-S3 DevKit. Cambialos aqui si tu placa usa otros.
#define PIN_MOTOR_IN1 5
#define PIN_MOTOR_IN2 6
#define PIN_MOTOR_PWM 7

#define PIN_ENCODER_A 15
#define PIN_ENCODER_B 16

#define PIN_LIMIT_CLOSED 17
#define PIN_LIMIT_OPEN 18
#define PIN_FTC_SENSOR 8

#define PIN_LOCAL_OPEN 11
#define PIN_LOCAL_CLOSE 12
#define PIN_LOCAL_STOP 13

#define PIN_I2C_SDA 9
#define PIN_I2C_SCL 10

#define PIN_BUZZER 21
#define PIN_LED_R 38
#define PIN_LED_G 39
#define PIN_LED_B 40

#define APP_INPUT_ACTIVE_LEVEL 0
#define APP_TIMER_PERIOD_MS 50
#define APP_LCD_REFRESH_MS 250
#define APP_TELEMETRY_PERIOD_MS 1000

typedef enum {
    FTC_STOP_ONLY = 0,
    FTC_STOP_AND_RESUME,
    FTC_STOP_AND_REVERSE
} ftc_behavior_t;

typedef enum {
    PORTON_INIT = 0,
    PORTON_CALIBRATION,
    PORTON_CLOSED,
    PORTON_OPENING,
    PORTON_OPEN,
    PORTON_CLOSING,
    PORTON_STOPPED,
    PORTON_OBSTRUCTED,
    PORTON_FAULT
} porton_state_t;

typedef enum {
    PORTON_CMD_NONE = 0,
    PORTON_CMD_OPEN,
    PORTON_CMD_CLOSE,
    PORTON_CMD_STOP,
    PORTON_CMD_TOGGLE,
    PORTON_CMD_RESET_FAULT,
    PORTON_CMD_CAL_START,
    PORTON_CMD_CAL_SET_CLOSED,
    PORTON_CMD_CAL_SET_OPEN,
    PORTON_CMD_CAL_JOG_OPEN,
    PORTON_CMD_CAL_JOG_CLOSE,
    PORTON_CMD_CAL_STOP
} porton_command_t;

typedef struct {
    uint32_t motor_pwm_freq_hz;
    uint32_t motor_pwm_duty_percent;
    bool encoder_enabled;
    bool encoder_inverted;
    int32_t encoder_counts_closed;
    int32_t encoder_counts_open;
    uint32_t movement_timeout_ms;
    uint32_t encoder_stall_timeout_ms;
    uint32_t obstruction_reverse_ms;
    bool auto_close_enabled;
    uint32_t auto_close_delay_ms;
    ftc_behavior_t ftc_behavior;
    bool buzzer_enabled;
    uint8_t lcd_i2c_addr;
} app_config_t;

static inline app_config_t app_config_default(void)
{
    return (app_config_t) {
        .motor_pwm_freq_hz = 20000,
        .motor_pwm_duty_percent = 70,
        .encoder_enabled = true,
        .encoder_inverted = false,
        .encoder_counts_closed = 0,
        .encoder_counts_open = 2000,
        .movement_timeout_ms = 20000,
        .encoder_stall_timeout_ms = 3000,
        .obstruction_reverse_ms = 1500,
        .auto_close_enabled = false,
        .auto_close_delay_ms = 10000,
        .ftc_behavior = FTC_STOP_AND_REVERSE,
        .buzzer_enabled = true,
        .lcd_i2c_addr = 0x3C,
    };
}
