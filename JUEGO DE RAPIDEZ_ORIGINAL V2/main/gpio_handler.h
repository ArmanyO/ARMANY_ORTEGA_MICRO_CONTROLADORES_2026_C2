#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BTN1_PIN    GPIO_NUM_4
#define BTN2_PIN    GPIO_NUM_13
#define LED1_PIN    GPIO_NUM_2
#define LED2_PIN    GPIO_NUM_15

typedef struct {
    bool stable_state;
    uint32_t last_change_time;
    bool last_raw_state;
} button_state_t;

void gpio_init(void);
bool gpio_read_btn1_raw(void);
bool gpio_read_btn2_raw(void);
void gpio_led1_on(void);
void gpio_led1_off(void);
void gpio_led2_on(void);
void gpio_led2_off(void);
void gpio_update_buttons(button_state_t *btn1, button_state_t *btn2, uint32_t now_us);

#ifdef __cplusplus
}
#endif

#endif