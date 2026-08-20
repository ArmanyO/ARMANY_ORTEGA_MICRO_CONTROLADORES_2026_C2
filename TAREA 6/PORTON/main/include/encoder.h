#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t encoder_init(bool inverted);
void encoder_set_enabled(bool enabled);
void encoder_set_inverted(bool inverted);
void encoder_reset(int32_t value);
int32_t encoder_get_count(void);
