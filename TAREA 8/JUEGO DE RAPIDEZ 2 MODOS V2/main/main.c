#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "mqtt_client.h"
#include "timer.h"

#define BTN1_PIN 4
#define BTN2_PIN 13
#define LED1_PIN 2
#define LED2_PIN 15

#define WIFI_SSID "Rapidito"
#define WIFI_PASS "Adm1N2584km"
#define MQTT_BROKER "mqtt://broker.emqx.io"

static const char *TAG = "reaction_game";

typedef enum {
    MODE_REACTION = 0,
    MODE_10_PRESSES = 1
} game_mode_t;

typedef enum {
    WAITING_START = 0,
    HOLDING_BTN1,
    LED1_ON_WAIT_RELEASE,
    WAIT_BTN2_PRESS,
    SHOW_RESULTS,
    MODE10_WAIT_FIRST,
    MODE10_COUNTING,
    MODE10_SHOW_RESULTS
} game_state_t;

game_mode_t game_mode = MODE_REACTION;
game_state_t state = WAITING_START;

volatile uint32_t led1_on_time = 0;
volatile uint32_t btn1_release_time = 0;
volatile uint32_t reaction1 = 0;
volatile uint32_t reaction2 = 0;

uint32_t mode10_press_times[10];
uint8_t mode10_press_count = 0;
uint32_t mode10_avg_time = 0;

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool wifi_connected = false;

static timer_t status_timer;

static void status_callback(void *arg) {
    if (wifi_connected && mqtt_client) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Estado: mode=%d state=%d uptime=%llu ms", 
                 game_mode, state, now_ms());
        esp_mqtt_client_publish(mqtt_client, "topic/qos0", msg, 0, 1, 0);
    }
}

static inline bool read_btn1(void) {
    return gpio_get_level(BTN1_PIN) == 0;
}

static inline bool read_btn2(void) {
    return gpio_get_level(BTN2_PIN) == 0;
}

static inline void led1_on(void) {
    gpio_set_level(LED1_PIN, 1);
}

static inline void led1_off(void) {
    gpio_set_level(LED1_PIN, 0);
}

static inline void led2_on(void) {
    gpio_set_level(LED2_PIN, 1);
}

static inline void led2_off(void) {
    gpio_set_level(LED2_PIN, 0);
}

static void publish_current_mode(void) {
    const char *mode_str = (game_mode == MODE_REACTION) ? "reaction" : "10presses";
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"mode\":\"%s\"}", mode_str);
    esp_mqtt_client_publish(mqtt_client, "reaction_game/mode", payload, 0, 1, 0);
    
    const char *msg = (game_mode == MODE_REACTION) 
        ? "Modo: REACCION. Presiona BTN1 (GPIO4) para empezar."
        : "Modo: 10 PULSACIONES. Presiona BTN2 (GPIO13) 10 veces.";
    esp_mqtt_client_publish(mqtt_client, "topic/qos0", msg, 0, 1, 0);
    ESP_LOGI(TAG, "Modo actual: %s", mode_str);
}

static timer_t blink_timer;
static int blink_count = 0;
static bool blink_state = false;

static void blink_callback(void *arg) {
    blink_state = !blink_state;
    if (blink_state) {
        led1_on();
        led2_on();
    } else {
        led1_off();
        led2_off();
    }
    blink_count++;
    if (blink_count >= 12) {
        timer_stop(&blink_timer);
        blink_count = 0;
    }
}

static void toggle_game_mode(void) {
    if (game_mode == MODE_REACTION) {
        game_mode = MODE_10_PRESSES;
        state = MODE10_WAIT_FIRST;
        mode10_press_count = 0;
        ESP_LOGI(TAG, "Cambiando a MODO 10 PULSACIONES");
    } else {
        game_mode = MODE_REACTION;
        state = WAITING_START;
        ESP_LOGI(TAG, "Cambiando a MODO REACCION");
    }
    publish_current_mode();
    
    timer_init(&blink_timer, blink_callback, NULL);
    timer_start_periodic(&blink_timer, 80000);
}

static void publish_times(void) {
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"reaction1_ms\":%lu,\"reaction2_ms\":%lu}", reaction1, reaction2);
    esp_mqtt_client_publish(mqtt_client, "reaction_game/times", payload, 0, 1, 0);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Reaccion 1: %lu ms | Reaccion 2: %lu ms", reaction1, reaction2);
    esp_mqtt_client_publish(mqtt_client, "topic/qos0", msg, 0, 1, 0);
    ESP_LOGI(TAG, "Datos publicados en MQTT");
}

static void publish_mode10_results(void) {
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"mode\":\"10presses\",\"avg_ms\":%lu,\"presses\":%d}", mode10_avg_time, mode10_press_count);
    esp_mqtt_client_publish(mqtt_client, "reaction_game/times", payload, 0, 1, 0);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Modo 10 pulsaciones - Promedio: %lu ms", mode10_avg_time);
    esp_mqtt_client_publish(mqtt_client, "topic/qos0", msg, 0, 1, 0);
    ESP_LOGI(TAG, "Resultados modo 10 pulsaciones publicados");
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Conectado");
            esp_mqtt_client_publish(mqtt_client, "topic/qos0", "ESP32 listo para jugar", 0, 1, 0);
            publish_current_mode();
            timer_init(&status_timer, status_callback, NULL);
            timer_start_periodic(&status_timer, 5000000);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Desconectado");
            timer_stop(&status_timer);
            break;
        default:
            break;
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGI(TAG, "Reconectando WiFi...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "WiFi Conectado! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        mqtt_app_start();
    }
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    
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
    
    ESP_LOGI(TAG, "WiFi inicializado, conectando...");
}

static void gpio_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN1_PIN) | (1ULL << BTN2_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    io_conf.pin_bit_mask = (1ULL << LED1_PIN) | (1ULL << LED2_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
    
    led1_off();
    led2_off();
}

void app_main(void) {
    ESP_LOGI(TAG, "Iniciando Juego de Rapidez ESP-IDF");
    
    gpio_init();
    wifi_init();
    
    uint32_t btn1_debounce = 0;
    uint32_t btn2_debounce = 0;
    bool btn1_last = false;
    bool btn2_last = false;
    bool btn1_stable = false;
    bool btn2_stable = false;
    bool btn1_prev = false;
    bool btn2_prev = false;
    
    uint32_t hold_start = 0;
    uint32_t both_hold_start = 0;
    bool both_prev = false;
    bool both_held_triggered = false;
    
    while (1) {
        if (!wifi_connected || !mqtt_client) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        uint64_t now = now_us();
        
        bool btn1_raw = read_btn1();
        if (btn1_raw != btn1_last) {
            btn1_debounce = now;
            btn1_last = btn1_raw;
        }
        if (elapsed_us(btn1_debounce) > 2000) {
            btn1_stable = btn1_raw;
        }
        
        bool btn2_raw = read_btn2();
        if (btn2_raw != btn2_last) {
            btn2_debounce = now;
            btn2_last = btn2_raw;
        }
        if (elapsed_us(btn2_debounce) > 2000) {
            btn2_stable = btn2_raw;
        }
        
        bool btn1_pressed = btn1_stable;
        bool btn1_fell = btn1_pressed && !btn1_prev;
        bool btn1_rose = !btn1_pressed && btn1_prev;
        btn1_prev = btn1_pressed;
        
        bool btn2_pressed = btn2_stable;
        bool btn2_fell = btn2_pressed && !btn2_prev;
        bool btn2_rose = !btn2_pressed && btn2_prev;
        btn2_prev = btn2_pressed;
        
        bool both_pressed = btn1_pressed && btn2_pressed;
        bool both_fell = both_pressed && !both_prev;
        bool both_rose = !both_pressed && both_prev;
        both_prev = both_pressed;
        
        if (both_fell) {
            both_hold_start = now;
            both_held_triggered = false;
        }
        if (both_rose) {
            both_hold_start = 0;
            both_held_triggered = false;
        }
        if (both_pressed && !both_held_triggered && both_hold_start > 0) {
            if (elapsed_us(both_hold_start) > 4000000) {
                both_held_triggered = true;
                toggle_game_mode();
            }
        }
        
        if (game_mode == MODE_REACTION) {
            switch (state) {
                case WAITING_START:
                    led1_off();
                    led2_off();
                    hold_start = 0;
                    if (btn1_fell) {
                        ESP_LOGI(TAG, "Boton 1 PRESIONADO. Esperando tiempo aleatorio...");
                        state = HOLDING_BTN1;
                    }
                    break;
                    
                case HOLDING_BTN1:
                    if (btn1_rose) {
                        ESP_LOGI(TAG, "Boton 1 soltado muy rapido. Reiniciando...");
                        hold_start = 0;
                        state = WAITING_START;
                        break;
                    }
                    
                    if (hold_start == 0) hold_start = now;
                    
                    uint32_t random_delay = (esp_random() % 3000000) + 1500000;
                    if (elapsed_us(hold_start) > random_delay) {
                        led1_on();
                        led1_on_time = now;
                        ESP_LOGI(TAG, "LED 1 ENCENDIDO! Suelta el Boton 1!");
                        state = LED1_ON_WAIT_RELEASE;
                        hold_start = 0;
                    }
                    break;
                    
                case LED1_ON_WAIT_RELEASE:
                    if (btn1_rose) {
                        btn1_release_time = now;
                        reaction1 = elapsed_ms(led1_on_time);
                        ESP_LOGI(TAG, "Reaccion 1: %lu ms. Presiona Boton 2 (GPIO13)...", reaction1);
                        led1_off();
                        state = WAIT_BTN2_PRESS;
                    }
                    break;
                    
                case WAIT_BTN2_PRESS:
                    if (btn2_fell) {
                        reaction2 = elapsed_ms(btn1_release_time);
                        ESP_LOGI(TAG, "Reaccion 2: %lu ms", reaction2);
                        led2_on();
                        state = SHOW_RESULTS;
                    }
                    break;
                    
                case SHOW_RESULTS:
                    publish_times();
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    led2_off();
                    state = WAITING_START;
                    break;
                    
                default:
                    state = WAITING_START;
                    break;
            }
        } else if (game_mode == MODE_10_PRESSES) {
            switch (state) {
                case MODE10_WAIT_FIRST:
                    led1_off();
                    led2_off();
                    mode10_press_count = 0;
                    if (btn2_fell) {
                        mode10_press_times[0] = now;
                        mode10_press_count = 1;
                        ESP_LOGI(TAG, "Primera pulsacion detectada. Continua...");
                        state = MODE10_COUNTING;
                    }
                    break;
                    
                case MODE10_COUNTING:
                    if (btn2_fell && mode10_press_count < 10) {
                        mode10_press_times[mode10_press_count] = now;
                        mode10_press_count++;
                        ESP_LOGI(TAG, "Pulsacion %d/10", mode10_press_count);
                        
                        if (mode10_press_count >= 10) {
                            uint32_t total_intervals = 0;
                            for (int i = 1; i < 10; i++) {
                                total_intervals += (mode10_press_times[i] - mode10_press_times[i-1]) / 1000;
                            }
                            mode10_avg_time = total_intervals / 9;
                            ESP_LOGI(TAG, "10 pulsaciones completadas. Promedio: %lu ms", mode10_avg_time);
                            led2_on();
                            state = MODE10_SHOW_RESULTS;
                        }
                    }
                    break;
                    
                case MODE10_SHOW_RESULTS:
                    publish_mode10_results();
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    led2_off();
                    state = MODE10_WAIT_FIRST;
                    break;
                    
                default:
                    state = MODE10_WAIT_FIRST;
                    break;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}