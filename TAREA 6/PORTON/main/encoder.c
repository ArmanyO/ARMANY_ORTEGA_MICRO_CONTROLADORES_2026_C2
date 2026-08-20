#include "encoder.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "encoder";
static volatile int32_t s_count = 0;
static volatile bool s_enabled = true;
static volatile bool s_inverted = false;

static void IRAM_ATTR encoder_isr(void *arg)
{
    (void)arg;

    if (!s_enabled) {
        return;
    }

    const int a = gpio_get_level(PIN_ENCODER_A);
    const int b = gpio_get_level(PIN_ENCODER_B);
    int delta = (a == b) ? 1 : -1;

    if (s_inverted) {
        delta = -delta;
    }
    s_count += delta;
}

esp_err_t encoder_init(bool inverted)
{
    s_inverted = inverted;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_A) | (1ULL << PIN_ENCODER_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ENCODER_A, encoder_isr, NULL));
    ESP_LOGI(TAG, "Encoder listo A=%d B=%d", PIN_ENCODER_A, PIN_ENCODER_B);
    return ESP_OK;
}

void encoder_set_enabled(bool enabled)
{
    s_enabled = enabled;
}

void encoder_set_inverted(bool inverted)
{
    s_inverted = inverted;
}

void encoder_reset(int32_t value)
{
    s_count = value;
}

int32_t encoder_get_count(void)
{
    return s_count;
}
