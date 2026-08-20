#pragma once

#include "config.h"
#include "io_manager.h"

void state_machine_init(const app_config_t *initial_config);
void state_machine_process(const app_inputs_t *inputs);
void state_machine_submit_command(porton_command_t command);
void state_machine_apply_config_message(const char *payload);
porton_state_t state_machine_get_state(void);
app_config_t state_machine_get_config(void);
