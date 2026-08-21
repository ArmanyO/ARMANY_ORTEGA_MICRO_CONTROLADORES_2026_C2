/*
 * Capa WiFi/MQTT opcional para portón
 * No toca la lógica de control, solo reporta estado y recibe comandos
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "porton_control.h"

static const char *TAG = "PORTON_WIFI_MQTT";

// Configuración - AJUSTA SEGÚN TU RED
#define WIFI_SSID      "Las Penas"
#define WIFI_PASS      "Pena123321"
#define MQTT_BROKER    "broker.emqx.io"
#define MQTT_PORT      1883
#define MQTT_TOPIC_CMD "porton/cmd"
#define MQTT_TOPIC_STA "porton/state"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;
static char s_client_id[48];

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_mqtt_connected = false;
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi desconectado, reintentando...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi conectado");
    }
}

static void publish_config(void) {
    if (!s_mqtt_connected) return;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "open_time_ms", porton_control_get_open_time_ms());
    cJSON_AddNumberToObject(json, "close_time_ms", porton_control_get_close_time_ms());
    cJSON_AddNumberToObject(json, "auto_close_ms", porton_control_get_auto_close_ms());
    cJSON_AddBoolToObject(json, "calibrated", porton_control_is_calibrated());

    char *out = cJSON_PrintUnformatted(json);
    if (out) {
        esp_mqtt_client_publish(s_mqtt_client, "porton/config", out, 0, 1, false);
        free(out);
    }
    cJSON_Delete(json);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_connected = true;
            esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_CMD, 1);
            ESP_LOGI(TAG, "MQTT conectado, suscrito a %s", MQTT_TOPIC_CMD);
            publish_config();
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT desconectado");
            break;
        case MQTT_EVENT_DATA: {
            if (event->topic_len == strlen(MQTT_TOPIC_CMD) &&
                strncmp(event->topic, MQTT_TOPIC_CMD, event->topic_len) == 0) {
                char payload[256] = {0};
                int len = event->data_len < sizeof(payload) - 1 ? event->data_len : sizeof(payload) - 1;
                memcpy(payload, event->data, len);
                ESP_LOGI(TAG, "Comando MQTT: %s", payload);

                cJSON *json = cJSON_Parse(payload);
                if (json) {
                    cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
                    if (cmd && cJSON_IsNumber(cmd)) {
                        switch (cmd->valueint) {
                            case 0: porton_control_open(); break;
                            case 1: porton_control_close(); break;
                            case 2: porton_control_stop(); break;
                            case 3: porton_control_toggle(); break;
                            case 4: porton_control_start_calibration(); break;
                            case 5: {
                                int pos = porton_control_get_position_pct();
                                cJSON *resp = cJSON_CreateObject();
                                cJSON_AddNumberToObject(resp, "position_pct", pos);
                                char *out = cJSON_PrintUnformatted(resp);
                                if (out) {
                                    esp_mqtt_client_publish(s_mqtt_client, "porton/position", out, 0, 1, false);
                                    free(out);
                                }
                                cJSON_Delete(resp);
                                break;
                            }
                            case 6: {
                                cJSON *ms = cJSON_GetObjectItem(json, "auto_close_ms");
                                if (ms && cJSON_IsNumber(ms)) {
                                    porton_control_set_auto_close_ms((uint32_t)ms->valueint);
                                }
                                break;
                            }
                            case 7: publish_config(); break;
                        }
                    }
                    cJSON_Delete(json);
                }
            }
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;
        default:
            break;
    }
}

static void state_callback(porton_state_t state, const char *event) {
    if (!s_mqtt_connected) return;

    cJSON *json = cJSON_CreateObject();
    const char *state_str[] = {"closed", "open", "opening", "closing", "wait_close", "obstructed", "error", "startup"};
    cJSON_AddStringToObject(json, "state", state_str[state]);
    cJSON_AddNumberToObject(json, "timestamp", (int)(esp_timer_get_time() / 1000));
    if (event) cJSON_AddStringToObject(json, "event", event);

    // Incluir posición si está calibrado
    int pos = porton_control_get_position_pct();
    if (pos >= 0) {
        cJSON_AddNumberToObject(json, "position_pct", pos);
    }

    // RSSI WiFi
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(json, "rssi", ap_info.rssi);
    }

    char *out = cJSON_PrintUnformatted(json);
    if (out) {
        esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_STA, out, 0, 1, false);
        free(out);
    }
    cJSON_Delete(json);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_mqtt_connected = false;
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi desconectado, reintentando...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi conectado");
    }
}

static void wifi_mqtt_task(void *arg) {
    (void)arg;

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {0};
    strncpy((char*)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid)-1);
    strncpy((char*)wifi_cfg.sta.password, WIFI_PASS, sizeof(wifi_cfg.sta.password)-1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // MQTT
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_client_id, sizeof(s_client_id), "porton-%02X%02X%02X", mac[3], mac[4], mac[5]);

    char uri[64];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", MQTT_BROKER, MQTT_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = s_client_id,
        .session.keepalive = 60,
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    // Registrar callback de estado
    porton_control_set_state_callback(state_callback);

    // Loop: publicar estado periódico cada 2s
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (s_mqtt_connected) {
            state_callback(porton_control_get_state(), "heartbeat");
        }
    }
}

void wifi_mqtt_start(void) {
    xTaskCreate(wifi_mqtt_task, "wifi_mqtt", 8192, NULL, 5, NULL);
}