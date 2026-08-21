#ifndef RC_SWITCH_H
#define RC_SWITCH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rc_switch_dev *rc_switch_handle_t;

rc_switch_handle_t rc_switch_create(int gpio_num, bool is_transmitter);
void rc_switch_delete(rc_switch_handle_t dev);
void rc_switch_enable_transmit(rc_switch_handle_t dev);
void rc_switch_disable_transmit(rc_switch_handle_t dev);
void rc_switch_enable_receive(rc_switch_handle_t dev);
void rc_switch_disable_receive(rc_switch_handle_t dev);
bool rc_switch_available(rc_switch_handle_t dev);
unsigned int rc_switch_get_received_value(rc_switch_handle_t dev);
void rc_switch_reset_available(rc_switch_handle_t dev);
void rc_switch_send(rc_switch_handle_t dev, unsigned int value, int length);
void rc_switch_send_tri_state(rc_switch_handle_t dev, const char *tri_state);
void rc_switch_set_protocol(rc_switch_handle_t dev, int protocol);
void rc_switch_set_pulse_length(rc_switch_handle_t dev, int pulse_length);
void rc_switch_set_repeat_transmit(rc_switch_handle_t dev, int repeat);

#ifdef __cplusplus
}
#endif

#endif // RC_SWITCH_H