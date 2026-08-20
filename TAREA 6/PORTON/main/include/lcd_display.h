#pragma once

#include <stdint.h>
#include "config.h"
#include "io_manager.h"
#include "esp_err.h"

esp_err_t lcd_display_init(uint8_t i2c_addr);
void lcd_display_show_status(porton_state_t state, int32_t encoder_count, const app_inputs_t *inputs, bool mqtt_connected);
const char *porton_state_name(porton_state_t state);
