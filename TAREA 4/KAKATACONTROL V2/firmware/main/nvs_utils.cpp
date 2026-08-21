#include "nvs_utils.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "NVS_UTILS";
static nvs_handle_t s_nvs_handle = 0;
static bool s_nvs_opened = false;

esp_err_t nvs_utils_open(const char *namespace_name, bool read_only) {
    if (s_nvs_opened) {
        nvs_utils_close();
    }

    esp_err_t ret = nvs_open(namespace_name, read_only ? NVS_READONLY : NVS_READWRITE, &s_nvs_handle);
    if (ret == ESP_OK) {
        s_nvs_opened = true;
    } else {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace_name, esp_err_to_name(ret));
    }
    return ret;
}

void nvs_utils_close(void) {
    if (s_nvs_opened) {
        nvs_close(s_nvs_handle);
        s_nvs_handle = 0;
        s_nvs_opened = false;
    }
}

esp_err_t nvs_utils_set_int(const char *key, int32_t value) {
    if (!s_nvs_opened) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_i32(s_nvs_handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
    }
    return ret;
}

esp_err_t nvs_utils_get_int(const char *key, int32_t *out_value) {
    if (!s_nvs_opened || !out_value) return ESP_ERR_INVALID_STATE;
    return nvs_get_i32(s_nvs_handle, key, out_value);
}

int32_t nvs_utils_get_int_or_default(const char *key, int32_t default_value) {
    int32_t value = default_value;
    nvs_utils_get_int(key, &value);
    return value;
}

esp_err_t nvs_utils_set_str(const char *key, const char *value) {
    if (!s_nvs_opened) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = nvs_set_str(s_nvs_handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs_handle);
    }
    return ret;
}

esp_err_t nvs_utils_get_str(const char *key, char *out_value, size_t max_len) {
    if (!s_nvs_opened || !out_value) return ESP_ERR_INVALID_STATE;
    size_t required_len = max_len;
    return nvs_get_str(s_nvs_handle, key, out_value, &required_len);
}