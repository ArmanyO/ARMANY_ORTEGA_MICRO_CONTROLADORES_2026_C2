idf_component_register(SRCS "main.cpp" "ssd1306.cpp" "mpu6050.cpp" "rc_switch.cpp" "adc_utils.cpp" "nvs_utils.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES driver i2c nvs esp_adc_cal rmt)