#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "gpio_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAME_WAITING_START,
    GAME_HOLDING_BTN1,
    GAME_LED1_ON_WAIT_RELEASE,
    GAME_WAIT_BTN2_PRESS,
    GAME_SHOW_RESULTS
} game_state_t;

typedef struct {
    game_state_t state;
    uint32_t hold_start_time;
    uint32_t led1_on_time;
    uint32_t btn1_release_time;
    uint32_t reaction1_ms;
    uint32_t reaction2_ms;
    uint32_t random_delay_us;
    uint32_t show_results_start;
    button_state_t btn1;
    button_state_t btn2;
} game_context_t;

void game_init(game_context_t *ctx);
void game_update(game_context_t *ctx, uint32_t now_us);
uint32_t game_get_reaction1(const game_context_t *ctx);
uint32_t game_get_reaction2(const game_context_t *ctx);
game_state_t game_get_state(const game_context_t *ctx);
bool game_results_ready(const game_context_t *ctx);
void game_clear_results(game_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif