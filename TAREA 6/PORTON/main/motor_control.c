#include "motor_control.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "motor";
static motor_direction_t s_direction = MOTOR_STOP;
static uint32_t s_pwm_freq_hz = 20000;

static uint32_t duty_to_raw(uint32_t duty_percent)
{
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    return (1023U * duty_percent) / 100U;
}

esp_err_t motor_control_init(const app_config_t *cfg)
{
    if (cfg != NULL) {
        s_pwm_freq_hz = cfg->motor_pwm_freq_hz;
    }

    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_IN1) | (1ULL << PIN_MOTOR_IN2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = s_pwm_freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = PIN_MOTOR_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    motor_control_stop();
    ESP_LOGI(TAG, "Motor listo IN1=%d IN2=%d PWM=%d", PIN_MOTOR_IN1, PIN_MOTOR_IN2, PIN_MOTOR_PWM);
    return ESP_OK;
}

void motor_control_apply_config(const app_config_t *cfg)
{
    if (cfg == NULL || cfg->motor_pwm_freq_hz == s_pwm_freq_hz) {
        return;
    }

    s_pwm_freq_hz = cfg->motor_pwm_freq_hz;
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, s_pwm_freq_hz);
}

void motor_control_drive(motor_direction_t direction, uint32_t duty_percent)
{
    s_direction = direction;

    if (direction == MOTOR_OPEN) {
        gpio_set_level(PIN_MOTOR_IN1, 1);
        gpio_set_level(PIN_MOTOR_IN2, 0);
    } else if (direction == MOTOR_CLOSE) {
        gpio_set_level(PIN_MOTOR_IN1, 0);
        gpio_set_level(PIN_MOTOR_IN2, 1);
    } else {
        motor_control_stop();
        return;
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_to_raw(duty_percent));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void motor_control_stop(void)
{
    s_direction = MOTOR_STOP;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    gpio_set_level(PIN_MOTOR_IN1, 0);
    gpio_set_level(PIN_MOTOR_IN2, 0);
}

motor_direction_t motor_control_get_direction(void)
{
    return s_direction;
}
