#include "global_timer.h"
#include "game_logic.h"
#include "wifi_mqtt.h"
#include "gpio_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static game_context_t s_game_ctx;

static void on_mqtt_data(uint32_t reaction1, uint32_t reaction2)
{
    (void)reaction1;
    (void)reaction2;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Iniciando Juego de Rapidez (ESP-IDF) ===");
    
    global_timer_init();
    gpio_init();
    game_init(&s_game_ctx);
    wifi_mqtt_init(on_mqtt_data);
    
    ESP_LOGI(TAG, "Sistema listo. Presiona Boton 1 (GPIO4) para comenzar.");

    uint32_t last_log_time = 0;
    
    while (1) {
        uint32_t now_us = (uint32_t)global_timer_now_us();
        game_update(&s_game_ctx, now_us);

        if (game_results_ready(&s_game_ctx)) {
            wifi_mqtt_publish_times(game_get_reaction1(&s_game_ctx), game_get_reaction2(&s_game_ctx));
            game_clear_results(&s_game_ctx);
        }

        if (global_timer_expired_ms(last_log_time, 5000)) {
            ESP_LOGI(TAG, "Estado: %d | Libre heap: %lu bytes", 
                     game_get_state(&s_game_ctx), (unsigned long)esp_get_free_heap_size());
            last_log_time = global_timer_now_ms();
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}