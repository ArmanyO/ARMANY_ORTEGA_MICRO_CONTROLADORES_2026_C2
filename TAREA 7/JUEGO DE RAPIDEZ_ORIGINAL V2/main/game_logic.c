#include "game_logic.h"
#include "global_timer.h"
#include "gpio_handler.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "game_logic";

static const uint32_t MIN_DELAY_US = 1500000;
static const uint32_t MAX_DELAY_US = 4500000;
static const uint32_t SHOW_RESULTS_TIME_MS = 2000;

void game_init(game_context_t *ctx)
{
    ctx->state = GAME_WAITING_START;
    ctx->hold_start_time = 0;
    ctx->led1_on_time = 0;
    ctx->btn1_release_time = 0;
    ctx->reaction1_ms = 0;
    ctx->reaction2_ms = 0;
    ctx->random_delay_us = 0;
    ctx->show_results_start = 0;
    ctx->btn1.stable_state = false;
    ctx->btn1.last_change_time = 0;
    ctx->btn1.last_raw_state = false;
    ctx->btn2.stable_state = false;
    ctx->btn2.last_change_time = 0;
    ctx->btn2.last_raw_state = false;
    
    ESP_LOGI(TAG, "Game initialized");
}

static void generate_random_delay(game_context_t *ctx)
{
    ctx->random_delay_us = MIN_DELAY_US + (esp_random() % (MAX_DELAY_US - MIN_DELAY_US));
}

void game_update(game_context_t *ctx, uint32_t now_us)
{
    gpio_update_buttons(&ctx->btn1, &ctx->btn2, now_us);

    bool btn1_pressed = ctx->btn1.stable_state;
    bool btn1_fell = btn1_pressed && !ctx->btn1.last_raw_state;
    bool btn1_rose = !btn1_pressed && ctx->btn1.last_raw_state;
    
    bool btn2_pressed = ctx->btn2.stable_state;
    bool btn2_fell = btn2_pressed && !ctx->btn2.last_raw_state;
    bool btn2_rose = !btn2_pressed && ctx->btn2.last_raw_state;

    ctx->btn1.last_raw_state = btn1_pressed;
    ctx->btn2.last_raw_state = btn2_pressed;

    switch (ctx->state) {
        case GAME_WAITING_START:
            gpio_led1_off();
            gpio_led2_off();
            ctx->hold_start_time = 0;
            ctx->show_results_start = 0;
            if (btn1_fell) {
                ESP_LOGI(TAG, "-> Boton 1 PRESIONADO. Esperando tiempo aleatorio...");
                generate_random_delay(ctx);
                ctx->state = GAME_HOLDING_BTN1;
            }
            break;

        case GAME_HOLDING_BTN1:
            if (btn1_rose) {
                ESP_LOGI(TAG, "-> Boton 1 soltado muy rapido. Reiniciando...");
                ctx->hold_start_time = 0;
                ctx->state = GAME_WAITING_START;
                break;
            }

            if (ctx->hold_start_time == 0) {
                ctx->hold_start_time = now_us;
            }

            if (global_timer_expired_us(ctx->hold_start_time, ctx->random_delay_us)) {
                gpio_led1_on();
                ctx->led1_on_time = now_us;
                ESP_LOGI(TAG, "-> LED 1 ENCENDIDO! Suelta el Boton 1!");
                ctx->state = GAME_LED1_ON_WAIT_RELEASE;
                ctx->hold_start_time = 0;
            }
            break;

        case GAME_LED1_ON_WAIT_RELEASE:
            if (btn1_rose) {
                ctx->btn1_release_time = now_us;
                ctx->reaction1_ms = global_timer_elapsed_us(ctx->led1_on_time) / 1000;
                ESP_LOGI(TAG, "-> Reaccion 1: %" PRIu32 " ms. Presiona Boton 2 (GPIO13)...", ctx->reaction1_ms);
                gpio_led1_off();
                ctx->state = GAME_WAIT_BTN2_PRESS;
            }
            break;

        case GAME_WAIT_BTN2_PRESS:
            if (btn2_fell) {
                uint32_t btn2_time = now_us;
                ctx->reaction2_ms = global_timer_elapsed_us(ctx->btn1_release_time) / 1000;
                ESP_LOGI(TAG, "-> Reaccion 2: %" PRIu32 " ms.", ctx->reaction2_ms);
                gpio_led2_on();
                ctx->state = GAME_SHOW_RESULTS;
                ctx->show_results_start = global_timer_now_ms();
            }
            break;

        case GAME_SHOW_RESULTS:
            if (global_timer_expired_ms(ctx->show_results_start, SHOW_RESULTS_TIME_MS)) {
                gpio_led2_off();
                ctx->state = GAME_WAITING_START;
            }
            break;
    }
}

uint32_t game_get_reaction1(const game_context_t *ctx)
{
    return ctx->reaction1_ms;
}

uint32_t game_get_reaction2(const game_context_t *ctx)
{
    return ctx->reaction2_ms;
}

game_state_t game_get_state(const game_context_t *ctx)
{
    return ctx->state;
}

bool game_results_ready(const game_context_t *ctx)
{
    return ctx->state == GAME_SHOW_RESULTS && 
           global_timer_expired_ms(ctx->show_results_start, SHOW_RESULTS_TIME_MS);
}

void game_clear_results(game_context_t *ctx)
{
    ctx->reaction1_ms = 0;
    ctx->reaction2_ms = 0;
}