#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
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
#include "driver/touch_sensor.h"
#include "driver/touch_pad.h"
#include "mqtt_client.h"

static const char *TAG = "PORTON_GAME";

// Hardware
#define START_TOUCH_PIN     13  // T4 / GPIO13
#define LED1_PIN            2   // Built-in LED
#define TARGET_TOUCH_PIN    4   // T0 / GPIO4
#define LED2_PIN            15  // LED2 output

// Touch config
#define START_TOUCH_DEBOUNCE_US       50000UL
#define START_RELEASE_DEBOUNCE_US     2000UL
#define TOUCH_DEBOUNCE_US             50000UL
#define TOUCH_CALIBRATION_MS          2000UL
#define TOUCH_SAMPLE_EVERY_MS         250UL

// Game timing
#define RANDOM_MIN_US                 1000000UL
#define RANDOM_MAX_US                 8000000UL
#define COOLDOWN_MS                   1500UL
#define PUBLISH_RETRY_MS              2000UL
#define WIFI_RETRY_MS                 5000UL
#define MQTT_RETRY_MS                 5000UL

// WiFi/MQTT
#define WIFI_SSID      "Las Penas"
#define WIFI_PASSWORD  "Pena123321"
#define MQTT_HOST      "broker.emqx.io"
#define MQTT_PORT      1883
#define MQTT_TOPIC     "reaction_game/times"

// Touch channels for ESP32
#define TOUCH_PAD_START   TOUCH_PAD_NUM4   // GPIO13 = T4
#define TOUCH_PAD_TARGET  TOUCH_PAD_NUM0   // GPIO4 = T0

typedef enum {
    WAIT_FOR_BOOT,
    RANDOM_DELAY,
    WAIT_FOR_RELEASE,
    WAIT_FOR_TOUCH,
    PUBLISH_RESULT,
    COOLDOWN
} GameState;

static GameState state = WAIT_FOR_BOOT;

static uint32_t startTouchCandidateUs = 0;
static bool startTouchCandidateActive = false;
static uint32_t waitStartUs = 0;
static uint32_t randomDelayUs = 0;
static uint32_t ledOnUs = 0;
static uint32_t releaseCandidateUs = 0;
static bool releaseCandidateActive = false;
static uint32_t releaseUs = 0;
static uint32_t touchCandidateUs = 0;
static bool touchCandidateActive = false;
static uint32_t reaction1Us = 0;
static uint32_t reaction2Us = 0;
static uint32_t cooldownStartMs = 0;
static uint32_t lastTouchPrintMs = 0;
static uint32_t lastWifiAttemptMs = 0;
static uint32_t lastMqttAttemptMs = 0;
static uint32_t lastPublishAttemptMs = 0;
static uint16_t startTouchBaseline = 0;
static uint16_t startTouchThreshold = 0;
static uint16_t targetTouchBaseline = 0;
static uint16_t targetTouchThreshold = 0;
static char resultJson[96] = {0};

static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool wifi_connected = false;
static bool mqtt_connected = false;
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static inline uint32_t elapsedUs(uint32_t sinceUs) {
    return esp_timer_get_time() - sinceUs;
}

static inline uint32_t usToRoundedMs(uint32_t valueUs) {
    return (valueUs + 500UL) / 1000UL;
}

static void clearLeds(void) {
    gpio_set_level(LED1_PIN, 0);
    gpio_set_level(LED2_PIN, 0);
}

static uint16_t thresholdFromBaseline(uint16_t baseline) {
    uint16_t th = (baseline * 70U) / 100U;
    return th < 5U ? 5U : th;
}

static void calibrateTouch(void) {
    ESP_LOGI(TAG, "Calibrando touch GPIO13/T4 y GPIO4/T0: no toques los pines...");

    uint32_t startTotal = 0;
    uint32_t targetTotal = 0;
    uint16_t samples = 0;
    uint32_t startMs = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - startMs) < TOUCH_CALIBRATION_MS) {
        uint16_t startVal, targetVal;
        touch_pad_read_raw_data(TOUCH_PAD_START, &startVal);
        touch_pad_read_raw_data(TOUCH_PAD_TARGET, &targetVal);
        startTotal += startVal;
        targetTotal += targetVal;
        samples++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    startTouchBaseline = samples > 0 ? startTotal / samples : 0;
    targetTouchBaseline = samples > 0 ? targetTotal / samples : 0;
    startTouchThreshold = thresholdFromBaseline(startTouchBaseline);
    targetTouchThreshold = thresholdFromBaseline(targetTouchBaseline);

    ESP_LOGI(TAG, "GPIO13 baseline=%u threshold=%u", startTouchBaseline, startTouchThreshold);
    ESP_LOGI(TAG, "GPIO4 baseline=%u threshold=%u", targetTouchBaseline, targetTouchThreshold);
    ESP_LOGI(TAG, "Comandos: c=recalibrar, 1/2 ajustar GPIO13, +/- ajustar GPIO4, r=reset");
}

static bool startTouchDetected(void) {
    uint16_t val;
    touch_pad_read_raw_data(TOUCH_PAD_START, &val);
    return val <= startTouchThreshold;
}

static bool targetTouchDetected(void) {
    uint16_t val;
    touch_pad_read_raw_data(TOUCH_PAD_TARGET, &val);
    return val <= targetTouchThreshold;
}

static void resetGame(const char *reason) {
    clearLeds();
    startTouchCandidateActive = false;
    releaseCandidateActive = false;
    touchCandidateActive = false;
    resultJson[0] = '\0';
    state = WAIT_FOR_BOOT;

    if (reason) {
        ESP_LOGI(TAG, "Reset: %s", reason);
    }
    ESP_LOGI(TAG, "Manten el dedo en GPIO13/T4 para iniciar ronda.");
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
            ESP_LOGD(TAG, "MQTT publicado, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;
        default:
            break;
    }
}

static void connectMqtt(void) {
    if (!wifi_connected || mqtt_connected) return;

    uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (lastMqttAttemptMs != 0 && nowMs - lastMqttAttemptMs < MQTT_RETRY_MS) return;
    lastMqttAttemptMs = nowMs;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char clientId[48];
    snprintf(clientId, sizeof(clientId), "reaction-game-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://" MQTT_HOST ":" STRINGIFY(MQTT_PORT),
        .credentials.client_id = clientId,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void serviceNetwork(void) {
    if (!wifi_connected) return;
    connectMqtt();
}

static void printTouchStatus(void) {
    uint16_t startRaw, targetRaw;
    touch_pad_read_raw_data(TOUCH_PAD_START, &startRaw);
    touch_pad_read_raw_data(TOUCH_PAD_TARGET, &targetRaw);
    ESP_LOGI(TAG, "GPIO13 raw=%u base=%u th=%u | GPIO4 raw=%u base=%u th=%u",
             startRaw, startTouchBaseline, startTouchThreshold,
             targetRaw, targetTouchBaseline, targetTouchThreshold);
}

static void pollSerialCommands(void) {
    int c = getchar();
    if (c < 0) return;

    char cmd = (char)c;
    if (cmd == 'c' && (state == WAIT_FOR_BOOT || state == COOLDOWN)) {
        calibrateTouch();
    } else if (cmd == '1') {
        startTouchThreshold += 1;
        printTouchStatus();
    } else if (cmd == '2' && startTouchThreshold > 1) {
        startTouchThreshold -= 1;
        printTouchStatus();
    } else if (cmd == '+') {
        targetTouchThreshold += 1;
        printTouchStatus();
    } else if (cmd == '-' && targetTouchThreshold > 1) {
        targetTouchThreshold -= 1;
        printTouchStatus();
    } else if (cmd == 'r') {
        resetGame("reset manual");
    }
}

static bool stableStartTouchReady(void) {
    if (!startTouchDetected()) {
        startTouchCandidateActive = false;
        return false;
    }
    if (!startTouchCandidateActive) {
        startTouchCandidateUs = esp_timer_get_time();
        startTouchCandidateActive = true;
        return false;
    }
    return elapsedUs(startTouchCandidateUs) >= START_TOUCH_DEBOUNCE_US;
}

static void enterRandomDelay(void) {
    clearLeds();
    randomDelayUs = RANDOM_MIN_US + (esp_random() % (RANDOM_MAX_US - RANDOM_MIN_US + 1));
    waitStartUs = esp_timer_get_time();
    state = RANDOM_DELAY;
    ESP_LOGI(TAG, "Ronda armada. Delay aleatorio aprox ms=%lu", randomDelayUs / 1000UL);
}

static void handleWaitForBoot(void) {
    serviceNetwork();

    uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (nowMs - lastTouchPrintMs >= TOUCH_SAMPLE_EVERY_MS) {
        lastTouchPrintMs = nowMs;
        printTouchStatus();
    }

    if (stableStartTouchReady()) {
        enterRandomDelay();
    }
}

static void handleRandomDelay(void) {
    serviceNetwork();

    if (!startTouchDetected()) {
        resetGame("quitaste GPIO13 antes del LED1");
        return;
    }

    if (elapsedUs(waitStartUs) >= randomDelayUs) {
        gpio_set_level(LED1_PIN, 1);
        ledOnUs = esp_timer_get_time();
        releaseCandidateActive = false;
        state = WAIT_FOR_RELEASE;
        ESP_LOGI(TAG, "LED1 ON: quita el dedo de GPIO13");
    }
}

static void handleWaitForRelease(void) {
    if (startTouchDetected()) {
        releaseCandidateActive = false;
        return;
    }
    if (!releaseCandidateActive) {
        releaseCandidateUs = esp_timer_get_time();
        releaseCandidateActive = true;
        return;
    }
    if (elapsedUs(releaseCandidateUs) >= START_RELEASE_DEBOUNCE_US) {
        releaseUs = releaseCandidateUs;
        reaction1Us = releaseUs - ledOnUs;
        gpio_set_level(LED1_PIN, 0);
        touchCandidateActive = false;
        state = WAIT_FOR_TOUCH;
        ESP_LOGI(TAG, "reaction1_ms=%lu", usToRoundedMs(reaction1Us));
        ESP_LOGI(TAG, "Toca GPIO4/T0");
    }
}

static void handleWaitForTouch(void) {
    if (targetTouchDetected()) {
        if (!touchCandidateActive) {
            touchCandidateUs = esp_timer_get_time();
            reaction2Us = touchCandidateUs - releaseUs;
            gpio_set_level(LED2_PIN, 1);
            touchCandidateActive = true;
            ESP_LOGI(TAG, "reaction2_ms=%lu", usToRoundedMs(reaction2Us));
            return;
        }
        if (elapsedUs(touchCandidateUs) >= TOUCH_DEBOUNCE_US) {
            snprintf(resultJson, sizeof(resultJson),
                     "{\"reaction1_ms\":%lu,\"reaction2_ms\":%lu}",
                     usToRoundedMs(reaction1Us), usToRoundedMs(reaction2Us));
            ESP_LOGI(TAG, "JSON listo: %s", resultJson);
            state = PUBLISH_RESULT;
            lastPublishAttemptMs = 0;
        }
        return;
    }
    if (touchCandidateActive) {
        gpio_set_level(LED2_PIN, 0);
        touchCandidateActive = false;
    }
}

static void handlePublishResult(void) {
    serviceNetwork();

    uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (lastPublishAttemptMs != 0 && nowMs - lastPublishAttemptMs < PUBLISH_RETRY_MS) {
        return;
    }
    lastPublishAttemptMs = nowMs;

    if (!mqtt_connected) {
        ESP_LOGW(TAG, "MQTT no conectado; reintentando...");
        return;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, resultJson, 0, 1, false);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "Publicado en %s: %s", MQTT_TOPIC, resultJson);
        cooldownStartMs = nowMs;
        state = COOLDOWN;
    } else {
        ESP_LOGW(TAG, "Publish MQTT fallo; reintentando...");
    }
}

static void handleCooldown(void) {
    serviceNetwork();

    uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (nowMs - cooldownStartMs >= COOLDOWN_MS && !startTouchDetected()) {
        resetGame("ronda completada");
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Iniciando ESP32 Reaction Game (PORTON)");

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
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED1_PIN) | (1ULL << LED2_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    clearLeds();

    touch_pad_init();
    touch_pad_config(TOUCH_PAD_START, 0);
    touch_pad_config(TOUCH_PAD_TARGET, 0);
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    touch_pad_set_meas_time(0x1000, 0x1000);
    touch_pad_filter_start(10);

    calibrateTouch();
    resetGame();

    while (1) {
        pollSerialCommands();

        switch (state) {
            case WAIT_FOR_BOOT:      handleWaitForBoot();      break;
            case RANDOM_DELAY:       handleRandomDelay();      break;
            case WAIT_FOR_RELEASE:   handleWaitForRelease();   break;
            case WAIT_FOR_TOUCH:     handleWaitForTouch();     break;
            case PUBLISH_RESULT:     handlePublishResult();    break;
            case COOLDOWN:           handleCooldown();         break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}