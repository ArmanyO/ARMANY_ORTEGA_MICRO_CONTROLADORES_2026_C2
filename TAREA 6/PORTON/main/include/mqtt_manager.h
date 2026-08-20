#pragma once

#include <stdbool.h>
#include "config.h"
#include "io_manager.h"
#include "esp_err.h"

typedef void (*mqtt_command_cb_t)(porton_command_t command, const char *payload);
typedef void (*mqtt_config_cb_t)(const char *payload);

esp_err_t mqtt_manager_start(mqtt_command_cb_t command_cb, mqtt_config_cb_t config_cb);
bool mqtt_manager_is_connected(void);
void mqtt_manager_publish_state(porton_state_t state, int32_t encoder_count, const app_inputs_t *inputs);
void mqtt_manager_publish_event(const char *event_name, const char *details);
void mqtt_manager_publish_fault(const char *fault_text);
void mqtt_manager_publish_config(const app_config_t *cfg);
