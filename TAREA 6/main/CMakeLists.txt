idf_component_register(SRCS "main.cpp" "porton_control.c" "wifi_mqtt.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi esp_netif esp_event mqtt nvs_flash)