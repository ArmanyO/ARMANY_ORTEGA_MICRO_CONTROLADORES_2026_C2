#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

static const char *TAG = "mqtt5_led";

// Configuración
#define LED_GPIO        2
#define WIFI_SSID       CONFIG_EXAMPLE_WIFI_SSID
#define WIFI_PASS       CONFIG_EXAMPLE_WIFI_PASSWORD
#define BROKER_URL      CONFIG_BROKER_URL
#define MQTT_TOPIC      "/topic/led"

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Inicializar LED
static void led_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_GPIO, 0);
    ESP_LOGI(TAG, "LED inicializado en GPIO %d", LED_GPIO);
}

// Controlar LED
static void led_set(bool on) {
    gpio_set_level(LED_GPIO, on ? 1 : 0);
    ESP_LOGI(TAG, "LED %s", on ? "ENCENDIDO" : "APAGADO");
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Reconectando WiFi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi conectado! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Inicializar WiFi
static void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

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

    ESP_LOGI(TAG, "Conectando a %s...", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado a WiFi");
    } else {
        ESP_LOGE(TAG, "Fallo WiFi");
    }
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Conectado!");
            esp_mqtt_client_subscribe(client, MQTT_TOPIC, 0);
            ESP_LOGI(TAG, "Suscrito a %s", MQTT_TOPIC);
            break;

        case MQTT_EVENT_DATA: {
            char topic[64] = {0};
            char data[64] = {0};
            memcpy(topic, event->topic, event->topic_len < sizeof(topic)-1 ? event->topic_len : sizeof(topic)-1);
            memcpy(data, event->data, event->data_len < sizeof(data)-1 ? event->data_len : sizeof(data)-1);
            
            ESP_LOGI(TAG, "MQTT Data: topic=%s data=%s", topic, data);
            
            if (strcmp(topic, MQTT_TOPIC) == 0) {
                if (strcmp(data, "ON") == 0 || strcmp(data, "1") == 0) {
                    led_set(true);
                } else if (strcmp(data, "OFF") == 0 || strcmp(data, "0") == 0) {
                    led_set(false);
                } else {
                    ESP_LOGW(TAG, "Comando desconocido: %s (usa ON/OFF/1/0)", data);
                }
            }
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Desconectado");
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;

        default:
            break;
    }
}

// Iniciar MQTT
static void mqtt_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URL,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void app_main(void) {
    ESP_LOGI(TAG, "Iniciando MQTT5 LED Control");
    
    ESP_ERROR_CHECK(nvs_flash_init());
    led_init();
    wifi_init();
    mqtt_start();
    
    ESP_LOGI(TAG, "Listo! Publica en topic: %s", MQTT_TOPIC);
    ESP_LOGI(TAG, "Comandos: ON, OFF, 1, 0");
}