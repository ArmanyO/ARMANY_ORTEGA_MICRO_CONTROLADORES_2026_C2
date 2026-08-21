#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "mqtt_client.h"
#include "pin_config.h"
#include "ssd1306.h"
#include "mpu6050.h"
#include "rc_switch.h"
#include "adc_utils.h"
#include "nvs_utils.h"

static const char *TAG = "KAKATA_CTRLR_433";

#define WIFI_SSID      "KAKATA_WIFI"
#define WIFI_PASS      "kakata123"
#define MQTT_BROKER    "192.168.1.100"
#define MQTT_PORT      1883
#define MQTT_TOPIC     "kakata/ctrl"
#define PUBLISH_MS     200

#define OLED_ADDR      0x3C
#define OLED_WIDTH     128
#define OLED_HEIGHT    64

#define DEBOUNCE_MS    20
#define NUM_BTNS       10
#define NUM_LEDS       6

typedef struct {
    uint8_t pin;
    bool state;
    uint32_t last_debounce;
} Button;

typedef struct {
    int joy0MT_min, joy0MT_max, joy0MT_center;
    int joy0MD_min, joy0MD_max, joy0MD_center;
    int joy1MT_min, joy1MT_max, joy1MT_center;
    int joy1MD_min, joy1MD_max, joy1MD_center;
} CalibData;

static Button buttons[NUM_BTNS] = {
    { PIN_JOY0_BTN, true, 0 },
    { PIN_JOY1_BTN, true, 0 },
    { PIN_BTN_0,    true, 0 },
    { PIN_BTN_1,    true, 0 },
    { PIN_BTN_2,    true, 0 },
    { PIN_BTN_3,    true, 0 },
    { PIN_BTNL1,    true, 0 },
    { PIN_BTNL2,    true, 0 },
    { PIN_BTNL3,    true, 0 },
    { PIN_BTNL4,    true, 0 },
};

static const uint8_t led_pins[NUM_LEDS] = {
    PIN_LED1, PIN_LED2, PIN_LED3,
    PIN_LED4, PIN_LED5, PIN_LED6,
};

static const float VBAT_DIVIDER = (100.0 + 22.0) / 22.0;
static const float VREF = 3.3;

static CalibData cal = { 0, 4095, 2048, 0, 4095, 2048, 0, 4095, 2048, 0, 4095, 2048 };

static ssd1306_handle_t oled = NULL;
static mpu6050_handle_t mpu = NULL;
static rc_switch_handle_t rf_send = NULL;
static rc_switch_handle_t rf_recv = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool adc_cali_enabled = false;

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool wifi_connected = false;
static bool mqtt_connected = false;
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static bool gyro_mode = false;

static inline uint32_t elapsedUs(uint32_t sinceUs) {
    return esp_timer_get_time() - sinceUs;
}

static inline uint32_t usToRoundedMs(uint32_t valueUs) {
    return (valueUs + 500UL) / 1000UL;
}

static void clearLeds(void) {
    for (int i = 0; i < NUM_LEDS; i++) gpio_set_level(led_pins[i], 1);
}

static void show_splash_animation(void) {
    ssd1306_clear(oled);
    ssd1306_set_font(oled, &Font_11x18);
    ssd1306_set_color(oled, SSD1306_WHITE);

    for (int x = OLED_WIDTH; x > (OLED_WIDTH - 6 * 18) / 2; x -= 6) {
        ssd1306_clear(oled);
        ssd1306_set_cursor(oled, x, 16);
        ssd1306_write_string(oled, "KKC433");
        ssd1306_update_screen(oled);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    for (int y = 0; y < OLED_HEIGHT / 2; y += 2) {
        ssd1306_clear(oled);
        ssd1306_set_cursor(oled, (OLED_WIDTH - 6 * 18) / 2, 16);
        ssd1306_write_string(oled, "KKC433");
        ssd1306_fill_rect(oled, 0, 0, OLED_WIDTH, y, SSD1306_WHITE);
        ssd1306_update_screen(oled);
        vTaskDelay(pdMS_TO_TICKS(12));
    }

    ssd1306_clear(oled);
    ssd1306_set_cursor(oled, (OLED_WIDTH - 6 * 18) / 2, 16);
    ssd1306_write_string(oled, "KKC433");
    ssd1306_update_screen(oled);
    vTaskDelay(pdMS_TO_TICKS(300));

    ssd1306_set_font(oled, &Font_7x10);
    ssd1306_set_cursor(oled, 20, 54);
    ssd1306_write_string(oled, "KAKATA RC433 V1");
    ssd1306_update_screen(oled);
    vTaskDelay(pdMS_TO_TICKS(600));
}

static void calibrate_controller(void) {
    ssd1306_clear(oled);
    ssd1306_set_font(oled, &Font_7x10);
    ssd1306_set_color(oled, SSD1306_WHITE);
    ssd1306_set_cursor(oled, 10, 0);
    ssd1306_write_string(oled, "CALIBRACION");
    ssd1306_set_cursor(oled, 0, 12);
    ssd1306_write_string(oled, "Suelta todos los");
    ssd1306_set_cursor(oled, 0, 20);
    ssd1306_write_string(oled, "botones y joysticks");
    ssd1306_set_cursor(oled, 35, 40);
    ssd1306_write_string(oled, "KKC433");
    ssd1306_update_screen(oled);
    vTaskDelay(pdMS_TO_TICKS(1500));

    nvs_utils_open("calib", false);
    ssd1306_clear(oled);
    ssd1306_set_cursor(oled, 0, 0);
    ssd1306_write_string(oled, "Centrando joysticks...");
    ssd1306_update_screen(oled);

    vTaskDelay(pdMS_TO_TICKS(500));
    int sum0MT = 0, sum0MD = 0, sum1MT = 0, sum1MD = 0;
    int samples = 32;
    for (int i = 0; i < samples; i++) {
        sum0MT += adc_utils_read_raw(PIN_JOY0_MT);
        sum0MD += adc_utils_read_raw(PIN_JOY0_MD);
        sum1MT += adc_utils_read_raw(PIN_JOY1_MT);
        sum1MD += adc_utils_read_raw(PIN_JOY1_MD);
        vTaskDelay(pdMS_TO_TICKS(10));

        int prog = (i * OLED_WIDTH) / (samples - 1);
        ssd1306_fill_rect(oled, 0, 20, prog, 8, SSD1306_WHITE);
        ssd1306_update_screen(oled);
    }

    cal.joy0MT_center = sum0MT / samples;
    cal.joy0MD_center = sum0MD / samples;
    cal.joy1MT_center = sum1MT / samples;
    cal.joy1MD_center = sum1MD / samples;

    cal.joy0MT_min = cal.joy0MT_center - 400;
    cal.joy0MT_max = cal.joy0MT_center + 400;
    cal.joy0MD_min = cal.joy0MD_center - 400;
    cal.joy0MD_max = cal.joy0MD_center + 400;
    cal.joy1MT_min = cal.joy1MT_center - 400;
    cal.joy1MT_max = cal.joy1MT_center + 400;
    cal.joy1MD_min = cal.joy1MD_center - 400;
    cal.joy1MD_max = cal.joy1MD_center + 400;

    ssd1306_clear(oled);
    ssd1306_set_cursor(oled, 0, 0);
    ssd1306_write_string(oled, "Calibrando giroscopio...");
    ssd1306_set_cursor(oled, 0, 10);
    ssd1306_write_string(oled, "No muevas el control");
    ssd1306_update_screen(oled);

    long gxSum = 0, gySum = 0, gzSum = 0;
    int gSamples = 200;
    for (int i = 0; i < gSamples; i++) {
        int16_t gx, gy, gz;
        if (mpu && mpu6050_test_connection(mpu)) {
            mpu6050_get_motion6(mpu, NULL, NULL, NULL, &gx, &gy, &gz);
            gxSum += gx;
            gySum += gy;
            gzSum += gz;
        }
        int prog = (i * OLED_WIDTH) / (gSamples - 1);
        ssd1306_fill_rect(oled, 0, 24, prog, 8, SSD1306_WHITE);
        ssd1306_update_screen(oled);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    int16_t gxOffset = -(gxSum / gSamples);
    int16_t gyOffset = -(gySum / gSamples);
    int16_t gzOffset = -(gzSum / gSamples);

    mpu6050_set_gyro_offset(mpu, gxOffset, gyOffset, gzOffset);

    nvs_utils_set_short("gx_off", gxOffset);
    nvs_utils_set_short("gy_off", gyOffset);
    nvs_utils_set_short("gz_off", gzOffset);

    ssd1306_clear(oled);
    ssd1306_set_cursor(oled, 0, 0);
    ssd1306_write_string(oled, "Mueve cada joystick");
    ssd1306_set_cursor(oled, 0, 8);
    ssd1306_write_string(oled, "al maximo en todas");
    ssd1306_set_cursor(oled, 0, 16);
    ssd1306_write_string(oled, "direcciones...");
    ssd1306_update_screen(oled);
    vTaskDelay(pdMS_TO_TICKS(1000));

    uint32_t t0 = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - t0) < 3000) {
        int v0MT = adc_utils_read_raw(PIN_JOY0_MT);
        int v0MD = adc_utils_read_raw(PIN_JOY0_MD);
        int v1MT = adc_utils_read_raw(PIN_JOY1_MT);
        int v1MD = adc_utils_read_raw(PIN_JOY1_MD);

        if (v0MT < cal.joy0MT_min) cal.joy0MT_min = v0MT;
        if (v0MT > cal.joy0MT_max) cal.joy0MT_max = v0MT;
        if (v0MD < cal.joy0MD_min) cal.joy0MD_min = v0MD;
        if (v0MD > cal.joy0MD_max) cal.joy0MD_max = v0MD;
        if (v1MT < cal.joy1MT_min) cal.joy1MT_min = v1MT;
        if (v1MT > cal.joy1MT_max) cal.joy1MT_max = v1MT;
        if (v1MD < cal.joy1MD_min) cal.joy1MD_min = v1MD;
        if (v1MD > cal.joy1MD_max) cal.joy1MD_max = v1MD;

        int remained = 3 - (xTaskGetTickCount() * portTICK_PERIOD_MS - t0) / 1000;
        ssd1306_fill_rect(oled, 0, 28, OLED_WIDTH, 8, SSD1306_BLACK);
        ssd1306_set_cursor(oled, 0, 28);
        char buf[32];
        snprintf(buf, sizeof(buf), "Tiempo: %ds", remained);
        ssd1306_write_string(oled, buf);
        ssd1306_update_screen(oled);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    nvs_utils_set_int("j0mt_min", cal.joy0MT_min);
    nvs_utils_set_int("j0mt_max", cal.joy0MT_max);
    nvs_utils_set_int("j0mt_med", cal.joy0MT_center);
    nvs_utils_set_int("j0md_min", cal.joy0MD_min);
    nvs_utils_set_int("j0md_max", cal.joy0MD_max);
    nvs_utils_set_int("j0md_med", cal.joy0MD_center);
    nvs_utils_set_int("j1mt_min", cal.joy1MT_min);
    nvs_utils_set_int("j1mt_max", cal.joy1MT_max);
    nvs_utils_set_int("j1mt_med", cal.joy1MT_center);
    nvs_utils_set_int("j1md_min", cal.joy1MD_min);
    nvs_utils_set_int("j1md_max", cal.joy1MD_max);
    nvs_utils_set_int("j1md_med", cal.joy1MD_center);
    nvs_utils_close();

    for (int i = 0; i < 3; i++) {
        ssd1306_clear(oled);
        ssd1306_set_font(oled, &Font_16x26);
        ssd1306_set_cursor(oled, 25, 20);
        ssd1306_write_string(oled, "LISTO!");
        ssd1306_update_screen(oled);
        for (int l = 0; l < NUM_LEDS; l++) gpio_set_level(led_pins[l], 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        for (int l = 0; l < NUM_LEDS; l++) gpio_set_level(led_pins[l], 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ssd1306_set_font(oled, &Font_7x10);
}

static void load_calibration(void) {
    nvs_utils_open("calib", true);
    int32_t test = 0;
    if (nvs_utils_get_int("j0mt_min", &test) == ESP_OK && test != -1) {
        cal.joy0MT_min    = test;
        cal.joy0MT_max    = nvs_utils_get_int_or_default("j0mt_max", 4095);
        cal.joy0MT_center = nvs_utils_get_int_or_default("j0mt_med", 2048);
        cal.joy0MD_min    = nvs_utils_get_int_or_default("j0md_min", 0);
        cal.joy0MD_max    = nvs_utils_get_int_or_default("j0md_max", 4095);
        cal.joy0MD_center = nvs_utils_get_int_or_default("j0md_med", 2048);
        cal.joy1MT_min    = nvs_utils_get_int_or_default("j1mt_min", 0);
        cal.joy1MT_max    = nvs_utils_get_int_or_default("j1mt_max", 4095);
        cal.joy1MT_center = nvs_utils_get_int_or_default("j1mt_med", 2048);
        cal.joy1MD_min    = nvs_utils_get_int_or_default("j1md_min", 0);
        cal.joy1MD_max    = nvs_utils_get_int_or_default("j1md_max", 4095);
        cal.joy1MD_center = nvs_utils_get_int_or_default("j1md_med", 2048);
    }
    int16_t gxOff = nvs_utils_get_short_or_default("gx_off", 0);
    int16_t gyOff = nvs_utils_get_short_or_default("gy_off", 0);
    int16_t gzOff = nvs_utils_get_short_or_default("gz_off", 0);
    if (gxOff != 0 || gyOff != 0 || gzOff != 0) {
        mpu6050_set_gyro_offset(mpu, gxOff, gyOff, gzOff);
    }
    nvs_utils_close();
}

static int normalize_axis(int raw, int minVal, int maxVal, int centerVal) {
    if (raw < centerVal) {
        return ((raw - minVal) * 100) / (centerVal - minVal) - 100;
    } else {
        return ((raw - centerVal) * 100) / (maxVal - centerVal);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        mqtt_connected = false;
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi conectado");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT conectado");
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            ESP_LOGI(TAG, "MQTT desconectado");
            break;
        case MQTT_EVENT_PUBLISHED:
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;
        default:
            break;
    }
}

static void connect_mqtt(void) {
    if (!wifi_connected || mqtt_connected) return;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char client_id[48];
    snprintf(client_id, sizeof(client_id), "kakata-ctrl-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char uri[64];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", MQTT_BROKER, MQTT_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = client_id,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void service_network(void) {
    if (!wifi_connected) return;
    connect_mqtt();
}

static bool is_pressed(Button *btn) {
    bool raw = !gpio_get_level(btn->pin);
    if (raw != btn->state) {
        btn->last_debounce = xTaskGetTickCount() * portTICK_PERIOD_MS;
        btn->state = raw;
    }
    return ((xTaskGetTickCount() * portTICK_PERIOD_MS - btn->last_debounce) > DEBOUNCE_MS) && (btn->state == true);
}

static float read_battery_voltage(void) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += adc_utils_read_raw(PIN_VBAT);
    }
    float avg = sum / 16.0f;
    uint32_t voltage_mv = 0;
    if (adc_cali_enabled) {
        adc_cali_raw_to_voltage(adc_cali_handle, (int)avg, &voltage_mv);
    } else {
        voltage_mv = (avg * 3300) / 4095;
    }
    return (voltage_mv / 1000.0f) * VBAT_DIVIDER;
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Iniciando KAKATA RC433 V1 (CTRLR_433)");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    for (int i = 0; i < NUM_LEDS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << led_pins[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(led_pins[i], 1);
    }

    for (int i = 0; i < NUM_BTNS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << buttons[i].pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }

    gpio_config_t joy_conf = {
        .pin_bit_mask = (1ULL << PIN_JOY0_MT) | (1ULL << PIN_JOY0_MD) |
                        (1ULL << PIN_JOY1_MT) | (1ULL << PIN_JOY1_MD),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&joy_conf);

    gpio_config_t vbat_conf = {
        .pin_bit_mask = (1ULL << PIN_VBAT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&vbat_conf);

    gpio_config_t rf_conf = {
        .pin_bit_mask = (1ULL << PIN_RF_ENABLE) | (1ULL << PIN_RF_TX),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rf_conf);
    gpio_set_level(PIN_RF_ENABLE, 0);

    gpio_config_t rf_rx_conf = {
        .pin_bit_mask = (1ULL << PIN_RF_RX),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rf_rx_conf);

    gpio_config_t mpu_int_conf = {
        .pin_bit_mask = (1ULL << PIN_MPU_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&mpu_int_conf);

    adc_utils_init();
    adc_cali_enabled = adc_utils_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_11, &adc_cali_handle);

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    oled = ssd1306_create(I2C_NUM_0, OLED_ADDR);
    if (oled == NULL) {
        ESP_LOGE(TAG, "Error creando OLED");
    } else {
        ssd1306_init(oled);
    }

    mpu = mpu6050_create(I2C_NUM_0, MPU6050_ADDR_DEFAULT);
    if (mpu == NULL) {
        ESP_LOGE(TAG, "Error creando MPU6050");
    } else {
        mpu6050_init(mpu);
        mpu6050_set_gyro_range(mpu, MPU6050_GYRO_FS_2000);
        mpu6050_set_accel_range(mpu, MPU6050_ACCEL_FS_16);
    }

    rf_send = rc_switch_create(PIN_RF_TX, true);
    rf_recv = rc_switch_create(PIN_RF_RX, false);

    load_calibration();
    show_splash_animation();

    ESP_LOGI(TAG, "KAKATA RC433 V1 iniciado");

    uint32_t cal_hold_start = 0;
    bool cal_holding = false;
    bool cal_done = false;
    bool btn01done = false;
    bool rf_has_data = false;
    unsigned int rf_value = 0;
    uint32_t last_pub = 0;
    uint32_t last_blink = 0;

    while (1) {
        int c = getchar();
        if (c >= 0) {
            // Could add serial commands here if needed
        }

        bool btn1 = !gpio_get_level(PIN_BTN_1);
        bool btn2 = !gpio_get_level(PIN_BTN_2);

        if (!btn1 || !btn2) {
            cal_done = false;
        }

        if (btn1 && btn2 && !cal_done) {
            if (!cal_holding) {
                cal_holding = true;
                cal_hold_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
            } else if ((xTaskGetTickCount() * portTICK_PERIOD_MS - cal_hold_start) >= 3000) {
                calibrate_controller();
                cal_holding = false;
                cal_done = true;
            }
        } else {
            cal_holding = false;
        }

        bool btn0 = !gpio_get_level(PIN_BTN_0);
        bool btn01 = btn0 && btn1;
        if (btn01 && !btn01done) {
            gyro_mode = !gyro_mode;
            btn01done = true;
            for (int l = 0; l < NUM_LEDS; l++) gpio_set_level(led_pins[l], 1);
            gpio_set_level(led_pins[0], 0);
            gpio_set_level(led_pins[1], 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            for (int l = 0; l < NUM_LEDS; l++) gpio_set_level(led_pins[l], 1);
        }
        if (!btn01 && btn01done) btn01done = false;

        int16_t ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
        if (mpu && mpu6050_test_connection(mpu)) {
            mpu6050_get_motion6(mpu, &ax, &ay, &az, &gx, &gy, &gz);
        }

        int joy0MT, joy0MD, joy1MT, joy1MD;

        if (gyro_mode) {
            float roll  = atan2f(-ay * (9.8f / 16384.0f), az * (9.8f / 16384.0f)) * 180.0f / 3.14159f;
            float pitch = atan2f(ax * (9.8f / 16384.0f), sqrtf(ay * ay + az * az) * (9.8f / 16384.0f)) * 180.0f / 3.14159f;
            joy0MT = (int)roll * 10;
            joy0MT = joy0MT > 100 ? 100 : (joy0MT < -100 ? -100 : joy0MT);
            joy0MD = (int)pitch * 10;
            joy0MD = joy0MD > 100 ? 100 : (joy0MD < -100 ? -100 : joy0MD);
            if (abs(joy0MT) < 5) joy0MT = 0;
            if (abs(joy0MD) < 5) joy0MD = 0;
            joy1MT = 0;
            joy1MD = 0;
        } else {
            int raw0MT = adc_utils_read_raw(PIN_JOY0_MT);
            int raw0MD = adc_utils_read_raw(PIN_JOY0_MD);
            int raw1MT = adc_utils_read_raw(PIN_JOY1_MT);
            int raw1MD = adc_utils_read_raw(PIN_JOY1_MD);
            joy0MT = normalize_axis(raw0MT, cal.joy0MT_min, cal.joy0MT_max, cal.joy0MT_center);
            joy0MD = normalize_axis(raw0MD, cal.joy0MD_min, cal.joy0MD_max, cal.joy0MD_center);
            joy1MT = normalize_axis(raw1MT, cal.joy1MT_min, cal.joy1MT_max, cal.joy1MT_center);
            joy1MD = normalize_axis(raw1MD, cal.joy1MD_min, cal.joy1MD_max, cal.joy1MD_center);
        }

        uint16_t btn_state = 0;
        for (int i = 0; i < NUM_BTNS; i++) {
            if (is_pressed(&buttons[i])) {
                btn_state |= (1 << i);
            }
        }

        float vbat = read_battery_voltage();

        if (rf_recv && rc_switch_available(rf_recv)) {
            rf_value = rc_switch_get_received_value(rf_recv);
            ESP_LOGI(TAG, "RF RX: %u", rf_value);
            rc_switch_reset_available(rf_recv);
            rf_has_data = true;
        }

        ssd1306_clear(oled);
        ssd1306_set_font(oled, &Font_7x10);
        ssd1306_set_color(oled, SSD1306_WHITE);

        char buf[64];
        ssd1306_set_cursor(oled, 0, 0);
        snprintf(buf, sizeof(buf), "%s J0:%+3d %+3d", gyro_mode ? "GY" : "JO", joy0MT, joy0MD);
        ssd1306_write_string(oled, buf);
        ssd1306_set_cursor(oled, 72, 0);
        snprintf(buf, sizeof(buf), "J1:%+3d %+3d", joy1MT, joy1MD);
        ssd1306_write_string(oled, buf);

        ssd1306_set_cursor(oled, 0, 10);
        snprintf(buf, sizeof(buf), "BTN:%04X", btn_state);
        ssd1306_write_string(oled, buf);

        ssd1306_set_cursor(oled, 0, 20);
        snprintf(buf, sizeof(buf), "GYRO:%4d %4d %4d", gx, gy, gz);
        ssd1306_write_string(oled, buf);

        ssd1306_set_cursor(oled, 0, 30);
        snprintf(buf, sizeof(buf), "BAT:%.2fV", vbat);
        ssd1306_write_string(oled, buf);

        ssd1306_set_cursor(oled, 0, 40);
        if (rf_has_data) {
            snprintf(buf, sizeof(buf), "RF:RX %u", rf_value);
        } else {
            snprintf(buf, sizeof(buf), "RF:idle");
        }
        ssd1306_write_string(oled, buf);

        if (cal_holding) {
            int remaining = 3 - (xTaskGetTickCount() * portTICK_PERIOD_MS - cal_hold_start) / 1000;
            ssd1306_set_cursor(oled, 0, 50);
            snprintf(buf, sizeof(buf), "CAL %ds", remaining + 1);
            ssd1306_write_string(oled, buf);
        } else if (wifi_connected) {
            ssd1306_set_cursor(oled, 80, 50);
            snprintf(buf, sizeof(buf), "MQTT:%s", mqtt_connected ? "OK" : "...");
            ssd1306_write_string(oled, buf);
        }

        ssd1306_update_screen(oled);

        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (wifi_connected && (now_ms - last_pub > PUBLISH_MS)) {
            if (!mqtt_connected) {
                connect_mqtt();
            }
            if (mqtt_connected) {
                int j0mt_pos = joy0MT > 0 ? joy0MT : 0;
                int j0mt_neg = joy0MT < 0 ? joy0MT : 0;
                int j0md_pos = joy0MD > 0 ? joy0MD : 0;
                int j0md_neg = joy0MD < 0 ? joy0MD : 0;
                int j1mt_pos = joy1MT > 0 ? joy1MT : 0;
                int j1mt_neg = joy1MT < 0 ? joy1MT : 0;
                int j1md_pos = joy1MD > 0 ? joy1MD : 0;
                int j1md_neg = joy1MD < 0 ? joy1MD : 0;

                snprintf(buf, sizeof(buf),
                    "{\"j0\":{\"mt\":[%d,%d],\"md\":[%d,%d]},\"j1\":{\"mt\":[%d,%d],\"md\":[%d,%d]},\"btn\":%u,\"gyr\":[%d,%d,%d],\"mod\":\"%s\",\"bat\":%.2f}",
                    j0mt_pos, j0mt_neg, j0md_pos, j0md_neg,
                    j1mt_pos, j1mt_neg, j1md_pos, j1md_neg,
                    btn_state, gx, gy, gz,
                    gyro_mode ? "GY" : "JO", vbat);
                esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, buf, 0, 1, false);
            }
            last_pub = now_ms;
        }

        if (now_ms - last_blink > 1000) {
            last_blink = now_ms;
            int level = gpio_get_level(PIN_LED1);
            gpio_set_level(PIN_LED1, !level);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}