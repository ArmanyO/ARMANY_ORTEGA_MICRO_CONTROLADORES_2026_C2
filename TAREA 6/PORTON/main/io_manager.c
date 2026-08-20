#include "io_manager.h"

#include "driver/gpio.h"

static bool s_level_active(int level)
{
    return level == APP_INPUT_ACTIVE_LEVEL;
}

static void configure_input(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static void configure_output(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, 0);
}

esp_err_t io_manager_init(void)
{
    configure_input(PIN_LIMIT_CLOSED);
    configure_input(PIN_LIMIT_OPEN);
    configure_input(PIN_FTC_SENSOR);
    configure_input(PIN_LOCAL_OPEN);
    configure_input(PIN_LOCAL_CLOSE);
    configure_input(PIN_LOCAL_STOP);

    configure_output(PIN_LED_R);
    configure_output(PIN_LED_G);
    configure_output(PIN_LED_B);
    configure_output(PIN_BUZZER);
    return ESP_OK;
}

app_inputs_t io_manager_read_inputs(void)
{
    return (app_inputs_t) {
        .limit_closed = s_level_active(gpio_get_level(PIN_LIMIT_CLOSED)),
        .limit_open = s_level_active(gpio_get_level(PIN_LIMIT_OPEN)),
        .ftc_blocked = s_level_active(gpio_get_level(PIN_FTC_SENSOR)),
        .local_open = s_level_active(gpio_get_level(PIN_LOCAL_OPEN)),
        .local_close = s_level_active(gpio_get_level(PIN_LOCAL_CLOSE)),
        .local_stop = s_level_active(gpio_get_level(PIN_LOCAL_STOP)),
    };
}

void io_manager_set_led(led_state_t state, bool blink_phase)
{
    bool r = false;
    bool g = false;
    bool b = false;

    switch (state) {
    case LED_STATE_RED:
        r = true;
        break;
    case LED_STATE_GREEN:
        g = true;
        break;
    case LED_STATE_YELLOW:
        r = true;
        g = true;
        break;
    case LED_STATE_YELLOW_BLINK:
        r = blink_phase;
        g = blink_phase;
        break;
    case LED_STATE_BLUE:
        b = true;
        break;
    case LED_STATE_WHITE:
        r = true;
        g = true;
        b = true;
        break;
    case LED_STATE_OFF:
    default:
        break;
    }

    gpio_set_level(PIN_LED_R, r);
    gpio_set_level(PIN_LED_G, g);
    gpio_set_level(PIN_LED_B, b);
}

void io_manager_set_buzzer(bool enabled)
{
    gpio_set_level(PIN_BUZZER, enabled ? 1 : 0);
}
