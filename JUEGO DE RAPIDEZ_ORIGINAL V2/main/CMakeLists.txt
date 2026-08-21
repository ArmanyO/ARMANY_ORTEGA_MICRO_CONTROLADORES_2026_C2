idf_component_register(SRCS "main.c" "global_timer.c" "game_logic.c" "wifi_mqtt.c" "gpio_handler.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_timer driver esp_wifi esp_event esp_netif nvs_flash mqtt)