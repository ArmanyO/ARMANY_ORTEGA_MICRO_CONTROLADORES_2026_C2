#include "mqtt_manager.h"

#include <stdio.h>
#include <string.h>
#include "lcd_display.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static mqtt_command_cb_t s_command_cb = NULL;
static mqtt_config_cb_t s_config_cb = NULL;
static char s_base_topic[64];

static void topic_make(char *out, size_t out_len, const char *suffix)
{
    snprintf(out, out_len, "%s/%s", s_base_topic, suffix);
}

static bool topic_ends_with(const char *topic, const char *suffix)
{
    const size_t topic_len = strlen(topic);
    const size_t suffix_len = strlen(suffix);
    return topic_len >= suffix_len && strcmp(topic + topic_len - suffix_len, suffix) == 0;
}

static void publish_availability(bool online)
{
    if (s_client == NULL) {
        return;
    }

    char topic[96];
    topic_make(topic, sizeof(topic), "availability");
    esp_mqtt_client_publish(s_client, topic, online ? "online" : "offline", 0, 1, 1);
}

static void subscribe_topics(void)
{
    char topic[96];

    topic_make(topic, sizeof(topic), "cmd/#");
    esp_mqtt_client_subscribe(s_client, topic, 1);

    topic_make(topic, sizeof(topic), "config/#");
    esp_mqtt_client_subscribe(s_client, topic, 1);

    topic_make(topic, sizeof(topic), "calibration/#");
    esp_mqtt_client_subscribe(s_client, topic, 1);
}

static void dispatch_mqtt_message(const char *topic, const char *payload)
{
    if (s_command_cb != NULL) {
        if (topic_ends_with(topic, "/cmd/open")) s_command_cb(PORTON_CMD_OPEN, payload);
        else if (topic_ends_with(topic, "/cmd/close")) s_command_cb(PORTON_CMD_CLOSE, payload);
        else if (topic_ends_with(topic, "/cmd/stop")) s_command_cb(PORTON_CMD_STOP, payload);
        else if (topic_ends_with(topic, "/cmd/toggle")) s_command_cb(PORTON_CMD_TOGGLE, payload);
        else if (topic_ends_with(topic, "/cmd/reset_fault")) s_command_cb(PORTON_CMD_RESET_FAULT, payload);
        else if (topic_ends_with(topic, "/calibration/start")) s_command_cb(PORTON_CMD_CAL_START, payload);
        else if (topic_ends_with(topic, "/calibration/set_closed")) s_command_cb(PORTON_CMD_CAL_SET_CLOSED, payload);
        else if (topic_ends_with(topic, "/calibration/set_open")) s_command_cb(PORTON_CMD_CAL_SET_OPEN, payload);
        else if (topic_ends_with(topic, "/calibration/jog_open")) s_command_cb(PORTON_CMD_CAL_JOG_OPEN, payload);
        else if (topic_ends_with(topic, "/calibration/jog_close")) s_command_cb(PORTON_CMD_CAL_JOG_CLOSE, payload);
        else if (topic_ends_with(topic, "/calibration/stop")) s_command_cb(PORTON_CMD_CAL_STOP, payload);
    }

    if (s_config_cb != NULL && topic_ends_with(topic, "/config/set")) {
        s_config_cb(payload);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT conectado");
        publish_availability(true);
        subscribe_topics();
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT desconectado");
        break;

    case MQTT_EVENT_DATA: {
        char topic[160] = {0};
        char payload[256] = {0};

        const int topic_len = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        const int data_len = event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1;

        memcpy(topic, event->topic, topic_len);
        memcpy(payload, event->data, data_len);
        ESP_LOGI(TAG, "MQTT RX topic=%s payload=%s", topic, payload);
        dispatch_mqtt_message(topic, payload);
        break;
    }

    default:
        break;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        ESP_LOGW(TAG, "WiFi desconectado, reintentando");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi con IP, iniciando MQTT");
        if (s_client != NULL) {
            esp_mqtt_client_start(s_client);
        }
    }
}

static esp_err_t wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, APP_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, APP_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

esp_err_t mqtt_manager_start(mqtt_command_cb_t command_cb, mqtt_config_cb_t config_cb)
{
    s_command_cb = command_cb;
    s_config_cb = config_cb;
    snprintf(s_base_topic, sizeof(s_base_topic), "porton/%s", APP_DEVICE_ID);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = APP_MQTT_BROKER_URI,
        .credentials.client_id = APP_DEVICE_ID,
        .session.keepalive = 60,
#if defined(MQTT_PROTOCOL_V_5)
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
#endif
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    return wifi_start();
}

bool mqtt_manager_is_connected(void)
{
    return s_connected;
}

void mqtt_manager_publish_state(porton_state_t state, int32_t encoder_count, const app_inputs_t *inputs)
{
    if (!s_connected || inputs == NULL) {
        return;
    }

    char topic[96];
    char payload[256];

    topic_make(topic, sizeof(topic), "state");
    snprintf(payload, sizeof(payload),
             "{\"state\":\"%s\",\"encoder\":%ld,\"limit_closed\":%s,\"limit_open\":%s,\"ftc_blocked\":%s}",
             porton_state_name(state),
             (long)encoder_count,
             inputs->limit_closed ? "true" : "false",
             inputs->limit_open ? "true" : "false",
             inputs->ftc_blocked ? "true" : "false");

    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);

    topic_make(topic, sizeof(topic), "telemetry");
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

void mqtt_manager_publish_event(const char *event_name, const char *details)
{
    if (!s_connected) {
        return;
    }

    char topic[96];
    char payload[192];
    topic_make(topic, sizeof(topic), "event");
    snprintf(payload, sizeof(payload), "{\"event\":\"%s\",\"details\":\"%s\"}", event_name, details ? details : "");
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
}

void mqtt_manager_publish_fault(const char *fault_text)
{
    if (!s_connected) {
        return;
    }

    char topic[96];
    char payload[192];
    topic_make(topic, sizeof(topic), "fault");
    snprintf(payload, sizeof(payload), "{\"fault\":\"%s\"}", fault_text ? fault_text : "unknown");
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);
}

void mqtt_manager_publish_config(const app_config_t *cfg)
{
    if (!s_connected || cfg == NULL) {
        return;
    }

    char topic[96];
    char payload[384];
    topic_make(topic, sizeof(topic), "config/status");
    snprintf(payload, sizeof(payload),
             "{\"motor_pwm_duty\":%lu,\"motor_pwm_freq\":%lu,\"encoder_enabled\":%s,"
             "\"encoder_counts_closed\":%ld,\"encoder_counts_open\":%ld,\"ftc_behavior\":%d,"
             "\"movement_timeout_ms\":%lu,\"auto_close_enabled\":%s,\"auto_close_delay_ms\":%lu}",
             (unsigned long)cfg->motor_pwm_duty_percent,
             (unsigned long)cfg->motor_pwm_freq_hz,
             cfg->encoder_enabled ? "true" : "false",
             (long)cfg->encoder_counts_closed,
             (long)cfg->encoder_counts_open,
             (int)cfg->ftc_behavior,
             (unsigned long)cfg->movement_timeout_ms,
             cfg->auto_close_enabled ? "true" : "false",
             (unsigned long)cfg->auto_close_delay_ms);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);
}
