#include "gpio_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "global_timer.h"

static const char *TAG = "gpio_handler";

static const uint32_t DEBOUNCE_TIME_US = 2000;

void gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN1_PIN) | (1ULL << BTN2_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << LED1_PIN) | (1ULL << LED2_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    gpio_led1_off();
    gpio_led2_off();

    ESP_LOGI(TAG, "GPIO initialized");
}

bool gpio_read_btn1_raw(void)
{
    return gpio_get_level(BTN1_PIN) == 0;
}

bool gpio_read_btn2_raw(void)
{
    return gpio_get_level(BTN2_PIN) == 0;
}

void gpio_led1_on(void)
{
    gpio_set_level(LED1_PIN, 1);
}

void gpio_led1_off(void)
{
    gpio_set_level(LED1_PIN, 0);
}

void gpio_led2_on(void)
{
    gpio_set_level(LED2_PIN, 1);
}

void gpio_led2_off(void)
{
    gpio_set_level(LED2_PIN, 0);
}

static void update_button(button_state_t *btn, bool raw_state, uint32_t now_us)
{
    if (raw_state != btn->last_raw_state) {
        btn->last_change_time = now_us;
        btn->last_raw_state = raw_state;
    }
    if (global_timer_expired_us(btn->last_change_time, DEBOUNCE_TIME_US)) {
        btn->stable_state = raw_state;
    }
}

void gpio_update_buttons(button_state_t *btn1, button_state_t *btn2, uint32_t now_us)
{
    update_button(btn1, gpio_read_btn1_raw(), now_us);
    update_button(btn2, gpio_read_btn2_raw(), now_us);
}