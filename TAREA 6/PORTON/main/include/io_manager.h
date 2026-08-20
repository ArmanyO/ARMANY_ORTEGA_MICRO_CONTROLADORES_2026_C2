#pragma once

#include <stdbool.h>
#include "config.h"
#include "esp_err.h"

typedef struct {
    bool limit_closed;
    bool limit_open;
    bool ftc_blocked;
    bool local_open;
    bool local_close;
    bool local_stop;
} app_inputs_t;

typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_RED,
    LED_STATE_GREEN,
    LED_STATE_YELLOW,
    LED_STATE_YELLOW_BLINK,
    LED_STATE_BLUE,
    LED_STATE_WHITE
} led_state_t;

esp_err_t io_manager_init(void);
app_inputs_t io_manager_read_inputs(void);
void io_manager_set_led(led_state_t state, bool blink_phase);
void io_manager_set_buzzer(bool enabled);
