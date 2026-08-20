#include "state_machine.h"

#include <stdio.h>
#include <string.h>
#include "encoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "io_manager.h"
#include "motor_control.h"
#include "mqtt_manager.h"

static const char *TAG = "state";

static app_config_t s_cfg;
static porton_state_t s_state = PORTON_INIT;
static porton_command_t s_pending_cmd = PORTON_CMD_NONE;
static int64_t s_state_enter_ms = 0;
static int64_t s_last_encoder_move_ms = 0;
static int32_t s_last_encoder_count = 0;
static bool s_resume_after_obstruction = false;
static motor_direction_t s_resume_direction = MOTOR_STOP;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void publish_transition(porton_state_t old_state, porton_state_t new_state, const char *why)
{
    char details[96];
    snprintf(details, sizeof(details), "%s->%s %s",
             porton_state_name(old_state), porton_state_name(new_state), why ? why : "");
    mqtt_manager_publish_event("state_change", details);
}

static void enter_state(porton_state_t new_state, const char *why)
{
    if (s_state == new_state) {
        return;
    }

    const porton_state_t old_state = s_state;
    s_state = new_state;
    s_state_enter_ms = now_ms();
    s_last_encoder_move_ms = s_state_enter_ms;
    s_last_encoder_count = encoder_get_count();
    ESP_LOGI(TAG, "%s -> %s (%s)", porton_state_name(old_state), porton_state_name(new_state), why ? why : "");
    publish_transition(old_state, new_state, why);
}

static void fault(const char *text)
{
    motor_control_stop();
    enter_state(PORTON_FAULT, text);
    mqtt_manager_publish_fault(text);
}

static void start_opening(const char *why)
{
    motor_control_drive(MOTOR_OPEN, s_cfg.motor_pwm_duty_percent);
    enter_state(PORTON_OPENING, why);
}

static void start_closing(const char *why)
{
    motor_control_drive(MOTOR_CLOSE, s_cfg.motor_pwm_duty_percent);
    enter_state(PORTON_CLOSING, why);
}

static void stop_motion(porton_state_t next_state, const char *why)
{
    motor_control_stop();
    enter_state(next_state, why);
}

static void update_outputs(void)
{
    const bool blink = ((now_ms() / 400) % 2) == 0;

    switch (s_state) {
    case PORTON_CLOSED:
        io_manager_set_led(LED_STATE_RED, blink);
        io_manager_set_buzzer(false);
        break;
    case PORTON_OPEN:
        io_manager_set_led(LED_STATE_GREEN, blink);
        io_manager_set_buzzer(false);
        break;
    case PORTON_OPENING:
    case PORTON_CLOSING:
        io_manager_set_led(LED_STATE_YELLOW_BLINK, blink);
        io_manager_set_buzzer(false);
        break;
    case PORTON_FAULT:
        io_manager_set_led(LED_STATE_YELLOW, blink);
        io_manager_set_buzzer(s_cfg.buzzer_enabled);
        break;
    case PORTON_CALIBRATION:
        io_manager_set_led(LED_STATE_BLUE, blink);
        io_manager_set_buzzer(false);
        break;
    case PORTON_OBSTRUCTED:
        io_manager_set_led(LED_STATE_WHITE, blink);
        io_manager_set_buzzer(s_cfg.buzzer_enabled && blink);
        break;
    case PORTON_STOPPED:
    default:
        io_manager_set_led(LED_STATE_OFF, blink);
        io_manager_set_buzzer(false);
        break;
    }
}

static void check_encoder_stall(void)
{
    if (!s_cfg.encoder_enabled || motor_control_get_direction() == MOTOR_STOP) {
        return;
    }

    const int32_t current = encoder_get_count();
    const int64_t current_ms = now_ms();
    if (current != s_last_encoder_count) {
        s_last_encoder_count = current;
        s_last_encoder_move_ms = current_ms;
        return;
    }

    if (current_ms - s_last_encoder_move_ms > (int64_t)s_cfg.encoder_stall_timeout_ms) {
        fault("encoder_sin_movimiento");
    }
}

static void check_movement_timeout(void)
{
    if (s_state != PORTON_OPENING && s_state != PORTON_CLOSING) {
        return;
    }

    if (now_ms() - s_state_enter_ms > (int64_t)s_cfg.movement_timeout_ms) {
        fault("timeout_movimiento");
    }
}

static void handle_obstruction(const app_inputs_t *inputs)
{
    if (!inputs->ftc_blocked || s_state != PORTON_CLOSING) {
        return;
    }

    motor_control_stop();
    s_resume_after_obstruction = false;
    s_resume_direction = MOTOR_STOP;

    if (s_cfg.ftc_behavior == FTC_STOP_AND_RESUME) {
        s_resume_after_obstruction = true;
        s_resume_direction = MOTOR_CLOSE;
        enter_state(PORTON_OBSTRUCTED, "ftc_stop_resume");
    } else if (s_cfg.ftc_behavior == FTC_STOP_AND_REVERSE) {
        s_resume_after_obstruction = true;
        s_resume_direction = MOTOR_OPEN;
        enter_state(PORTON_OBSTRUCTED, "ftc_stop_reverse");
    } else {
        enter_state(PORTON_OBSTRUCTED, "ftc_stop_only");
    }
}

static void handle_command(porton_command_t cmd, const app_inputs_t *inputs)
{
    if (cmd == PORTON_CMD_NONE) {
        return;
    }

    if (cmd == PORTON_CMD_STOP || inputs->local_stop) {
        stop_motion(PORTON_STOPPED, "stop");
        return;
    }

    if (cmd == PORTON_CMD_RESET_FAULT && s_state == PORTON_FAULT) {
        if (inputs->limit_closed && inputs->limit_open) {
            mqtt_manager_publish_fault("no_reset_limits_imposibles");
        } else {
            stop_motion(PORTON_STOPPED, "reset_fault");
        }
        return;
    }

    if (s_state == PORTON_FAULT) {
        return;
    }

    if (cmd == PORTON_CMD_CAL_START) {
        stop_motion(PORTON_CALIBRATION, "calibration_start");
        return;
    }

    if (s_state == PORTON_CALIBRATION) {
        if (cmd == PORTON_CMD_CAL_SET_CLOSED) {
            s_cfg.encoder_counts_closed = encoder_get_count();
            mqtt_manager_publish_config(&s_cfg);
        } else if (cmd == PORTON_CMD_CAL_SET_OPEN) {
            s_cfg.encoder_counts_open = encoder_get_count();
            mqtt_manager_publish_config(&s_cfg);
        } else if (cmd == PORTON_CMD_CAL_JOG_OPEN) {
            motor_control_drive(MOTOR_OPEN, 35);
        } else if (cmd == PORTON_CMD_CAL_JOG_CLOSE) {
            motor_control_drive(MOTOR_CLOSE, 35);
        } else if (cmd == PORTON_CMD_CAL_STOP) {
            stop_motion(PORTON_STOPPED, "calibration_stop");
        }
        return;
    }

    if (cmd == PORTON_CMD_TOGGLE) {
        if (s_state == PORTON_OPEN || s_state == PORTON_OPENING) {
            cmd = PORTON_CMD_CLOSE;
        } else {
            cmd = PORTON_CMD_OPEN;
        }
    }

    if (cmd == PORTON_CMD_OPEN) {
        if (!inputs->limit_open) {
            start_opening("cmd_open");
        } else {
            stop_motion(PORTON_OPEN, "already_open");
        }
    } else if (cmd == PORTON_CMD_CLOSE) {
        if (!inputs->limit_closed) {
            start_closing("cmd_close");
        } else {
            stop_motion(PORTON_CLOSED, "already_closed");
        }
    }
}

static bool payload_has(const char *payload, const char *needle)
{
    return payload != NULL && strstr(payload, needle) != NULL;
}

static bool parse_uint_after(const char *payload, const char *key, uint32_t *out)
{
    const char *pos = strstr(payload, key);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }

    unsigned long value = 0;
    if (sscanf(pos + 1, "%lu", &value) != 1) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

void state_machine_init(const app_config_t *initial_config)
{
    s_cfg = initial_config != NULL ? *initial_config : app_config_default();
    encoder_set_enabled(s_cfg.encoder_enabled);
    encoder_set_inverted(s_cfg.encoder_inverted);
    motor_control_apply_config(&s_cfg);
    s_state = PORTON_INIT;
    s_state_enter_ms = now_ms();
    enter_state(PORTON_STOPPED, "init_done");
}

void state_machine_process(const app_inputs_t *inputs)
{
    if (inputs == NULL) {
        return;
    }

    if (inputs->limit_closed && inputs->limit_open) {
        fault("limit_closed_y_open_activos");
        update_outputs();
        return;
    }

    porton_command_t cmd = s_pending_cmd;
    s_pending_cmd = PORTON_CMD_NONE;

    if (inputs->local_open) {
        cmd = PORTON_CMD_OPEN;
    } else if (inputs->local_close) {
        cmd = PORTON_CMD_CLOSE;
    } else if (inputs->local_stop) {
        cmd = PORTON_CMD_STOP;
    }

    handle_command(cmd, inputs);

    if (s_state == PORTON_OPENING && inputs->limit_open) {
        encoder_reset(s_cfg.encoder_counts_open);
        stop_motion(PORTON_OPEN, "limit_open");
    } else if (s_state == PORTON_CLOSING && inputs->limit_closed) {
        encoder_reset(s_cfg.encoder_counts_closed);
        stop_motion(PORTON_CLOSED, "limit_closed");
    }

    handle_obstruction(inputs);

    if (s_state == PORTON_OBSTRUCTED && !inputs->ftc_blocked && s_resume_after_obstruction) {
        s_resume_after_obstruction = false;
        if (s_resume_direction == MOTOR_OPEN) {
            start_opening("ftc_clear_reverse");
        } else if (s_resume_direction == MOTOR_CLOSE) {
            start_closing("ftc_clear_resume");
        }
    }

    check_encoder_stall();
    check_movement_timeout();
    update_outputs();
}

void state_machine_submit_command(porton_command_t command)
{
    s_pending_cmd = command;
}

void state_machine_apply_config_message(const char *payload)
{
    if (payload == NULL) {
        return;
    }

    uint32_t value = 0;
    if (parse_uint_after(payload, "motor_pwm_duty", &value) && value <= 100) {
        s_cfg.motor_pwm_duty_percent = value;
    }
    if (parse_uint_after(payload, "motor_pwm_freq", &value) && value >= 100 && value <= 40000) {
        s_cfg.motor_pwm_freq_hz = value;
    }
    if (parse_uint_after(payload, "movement_timeout_ms", &value)) {
        s_cfg.movement_timeout_ms = value;
    }
    if (parse_uint_after(payload, "auto_close_delay_ms", &value)) {
        s_cfg.auto_close_delay_ms = value;
    }

    if (payload_has(payload, "STOP_ONLY")) {
        s_cfg.ftc_behavior = FTC_STOP_ONLY;
    } else if (payload_has(payload, "STOP_AND_RESUME")) {
        s_cfg.ftc_behavior = FTC_STOP_AND_RESUME;
    } else if (payload_has(payload, "STOP_AND_REVERSE")) {
        s_cfg.ftc_behavior = FTC_STOP_AND_REVERSE;
    }

    if (payload_has(payload, "\"encoder_enabled\":false")) {
        s_cfg.encoder_enabled = false;
    } else if (payload_has(payload, "\"encoder_enabled\":true")) {
        s_cfg.encoder_enabled = true;
    }

    motor_control_apply_config(&s_cfg);
    encoder_set_enabled(s_cfg.encoder_enabled);
    mqtt_manager_publish_config(&s_cfg);
}

porton_state_t state_machine_get_state(void)
{
    return s_state;
}

app_config_t state_machine_get_config(void)
{
    return s_cfg;
}
