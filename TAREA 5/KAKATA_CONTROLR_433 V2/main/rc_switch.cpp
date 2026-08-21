#include "rc_switch.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "RC_SWITCH";

#define RC_SWITCH_MAX_CHANGES 67
#define RC_SWITCH_DEFAULT_PROTOCOL 1
#define RC_SWITCH_DEFAULT_PULSE_LENGTH 350
#define RC_SWITCH_DEFAULT_REPEAT 10

typedef struct {
    int gpio_num;
    bool is_transmitter;
    rmt_channel_t rmt_channel;
    int protocol;
    int pulse_length;
    int repeat_transmit;
    volatile unsigned int received_value;
    volatile bool available;
    volatile bool receiving;
    volatile uint32_t last_time;
    volatile int change_count;
    volatile uint32_t durations[RC_SWITCH_MAX_CHANGES];
} rc_switch_dev_t;

static const struct {
    uint16_t pulse_length;
    uint8_t sync_factor;
    uint8_t zero_high;
    uint8_t zero_low;
    uint8_t one_high;
    uint8_t one_low;
} proto_timing[] = {
    {0, 0, 0, 0, 0, 0}, // placeholder for index 0
    {350, 31, 1, 3, 3, 1},    // Protocol 1
    {650, 10, 1, 2, 2, 1},    // Protocol 2
    {100, 30, 4, 11, 9, 6},   // Protocol 3
    {380, 34, 1, 2, 2, 1},    // Protocol 4
    {500, 40, 1, 2, 2, 1},    // Protocol 5
};

static void IRAM_ATTR rc_switch_isr_handler(void *arg) {
    rc_switch_dev_t *dev = (rc_switch_dev_t *)arg;
    if (!dev->receiving || dev->is_transmitter) return;

    uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    uint32_t duration = now - dev->last_time;
    dev->last_time = now;

    if (duration > 5000) {
        if (dev->change_count >= 6) {
            dev->available = true;
        }
        dev->change_count = 0;
        return;
    }

    if (dev->change_count < RC_SWITCH_MAX_CHANGES) {
        dev->durations[dev->change_count++] = duration;
    }
}

static bool rc_switch_decode(rc_switch_dev_t *dev) {
    if (dev->change_count < 6) return false;

    unsigned int code = 0;
    int delay = dev->durations[0] / 31;
    int tolerance = delay / 4;

    for (int i = 1; i < dev->change_count; i += 2) {
        if (i + 1 >= dev->change_count) break;

        int high = dev->durations[i];
        int low = dev->durations[i + 1];

        if (abs(high - delay) < tolerance && abs(low - delay * 3) < tolerance) {
            code = (code << 1) | 0;
        } else if (abs(high - delay * 3) < tolerance && abs(low - delay) < tolerance) {
            code = (code << 1) | 1;
        } else {
            return false;
        }
    }

    if (code != 0) {
        dev->received_value = code;
        return true;
    }
    return false;
}

rc_switch_handle_t rc_switch_create(int gpio_num, bool is_transmitter) {
    rc_switch_dev_t *dev = (rc_switch_dev_t *)calloc(1, sizeof(rc_switch_dev_t));
    if (!dev) return NULL;

    dev->gpio_num = gpio_num;
    dev->is_transmitter = is_transmitter;
    dev->protocol = RC_SWITCH_DEFAULT_PROTOCOL;
    dev->pulse_length = RC_SWITCH_DEFAULT_PULSE_LENGTH;
    dev->repeat_transmit = RC_SWITCH_DEFAULT_REPEAT;

    if (is_transmitter) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_num),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(gpio_num, 0);

        rmt_config_t rmt_conf = {
            .rmt_mode = RMT_MODE_TX,
            .channel = RMT_CHANNEL_0,
            .gpio_num = gpio_num,
            .mem_block_num = 1,
            .clk_div = 80,
            .tx_config = {
                .carrier_en = false,
                .idle_level = 0,
                .idle_output_en = true,
            },
        };
        rmt_config(&rmt_conf);
        rmt_driver_install(rmt_conf.channel, 0, 0);
        dev->rmt_channel = rmt_conf.channel;
    } else {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_num),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        gpio_config(&io_conf);
        gpio_install_isr_service(0);
        gpio_isr_handler_add(gpio_num, rc_switch_isr_handler, dev);
        dev->receiving = true;
    }

    return dev;
}

void rc_switch_delete(rc_switch_handle_t dev) {
    if (!dev) return;
    if (!dev->is_transmitter) {
        gpio_isr_handler_remove(dev->gpio_num);
        dev->receiving = false;
    } else {
        rmt_driver_uninstall(dev->rmt_channel);
    }
    free(dev);
}

void rc_switch_enable_transmit(rc_switch_handle_t dev) {
    if (!dev || !dev->is_transmitter) return;
}

void rc_switch_disable_transmit(rc_switch_handle_t dev) {
    if (!dev || !dev->is_transmitter) return;
}

void rc_switch_enable_receive(rc_switch_handle_t dev) {
    if (!dev || dev->is_transmitter) return;
    dev->receiving = true;
}

void rc_switch_disable_receive(rc_switch_handle_t dev) {
    if (!dev || dev->is_transmitter) return;
    dev->receiving = false;
}

bool rc_switch_available(rc_switch_handle_t dev) {
    if (!dev || dev->is_transmitter) return false;
    return dev->available;
}

unsigned int rc_switch_get_received_value(rc_switch_handle_t dev) {
    if (!dev || dev->is_transmitter) return 0;
    return dev->received_value;
}

void rc_switch_reset_available(rc_switch_handle_t dev) {
    if (!dev || dev->is_transmitter) return;
    dev->available = false;
    dev->change_count = 0;
}

static void rc_switch_build_waveform(rc_switch_handle_t dev, unsigned int code, int length, rmt_item32_t *items, int *num_items) {
    int idx = 0;
    int proto = dev->protocol;
    if (proto < 1 || proto > 5) proto = 1;

    int pulse = dev->pulse_length;
    int sync = pulse * proto_timing[proto].sync_factor;
    int zero_high = pulse * proto_timing[proto].zero_high;
    int zero_low = pulse * proto_timing[proto].zero_low;
    int one_high = pulse * proto_timing[proto].one_high;
    int one_low = pulse * proto_timing[proto].one_low;

    auto add_pulse = [&](int high_us, int low_us) {
        if (idx < 256) {
            items[idx++] = (rmt_item32_t){{high_us, 1, low_us, 0}};
        }
    };

    add_pulse(pulse, sync);

    for (int i = length - 1; i >= 0; i--) {
        if (code & (1 << i)) {
            add_pulse(one_high, one_low);
        } else {
            add_pulse(zero_high, zero_low);
        }
    }

    *num_items = idx;
}

void rc_switch_send(rc_switch_handle_t dev, unsigned int value, int length) {
    if (!dev || !dev->is_transmitter) return;

    rmt_item32_t items[256];
    int num_items = 0;
    rc_switch_build_waveform(dev, value, length, items, &num_items);

    for (int r = 0; r < dev->repeat_transmit; r++) {
        rmt_write_items(dev->rmt_channel, items, num_items, true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void rc_switch_send_tri_state(rc_switch_handle_t dev, const char *tri_state) {
    if (!dev || !dev->is_transmitter || !tri_state) return;
    // Simplified - would need full implementation for tri-state
}

void rc_switch_set_protocol(rc_switch_handle_t dev, int protocol) {
    if (!dev) return;
    if (protocol >= 1 && protocol <= 5) {
        dev->protocol = protocol;
    }
}

void rc_switch_set_pulse_length(rc_switch_handle_t dev, int pulse_length) {
    if (!dev) return;
    dev->pulse_length = pulse_length;
}

void rc_switch_set_repeat_transmit(rc_switch_handle_t dev, int repeat) {
    if (!dev) return;
    dev->repeat_transmit = repeat;
}