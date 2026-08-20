idf_component_register(
    SRCS
        "app_main.c"
        "encoder.c"
        "io_manager.c"
        "lcd_display.c"
        "motor_control.c"
        "mqtt_manager.c"
        "state_machine.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_event
        esp_netif
        esp_wifi
        mqtt
        nvs_flash
)
