idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_wifi esp_netif esp_event mqtt nvs_flash)