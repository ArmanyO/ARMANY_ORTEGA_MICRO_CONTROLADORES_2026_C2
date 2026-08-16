idf_component_register(SRCS "main.c"
    REQUIRES mqtt driver nvs_flash esp_netif esp_event esp_wifi freertos
    INCLUDE_DIRS ".")