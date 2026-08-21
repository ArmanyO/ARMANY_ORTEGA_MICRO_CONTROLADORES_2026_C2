#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_utils_open(const char *namespace_name, bool read_only);
void nvs_utils_close(void);
esp_err_t nvs_utils_set_int(const char *key, int32_t value);
esp_err_t nvs_utils_get_int(const char *key, int32_t *out_value);
int32_t nvs_utils_get_int_or_default(const char *key, int32_t default_value);
esp_err_t nvs_utils_set_short(const char *key, int16_t value);
esp_err_t nvs_utils_get_short(const char *key, int16_t *out_value);
int16_t nvs_utils_get_short_or_default(const char *key, int16_t default_value);
esp_err_t nvs_utils_set_str(const char *key, const char *value);
esp_err_t nvs_utils_get_str(const char *key, char *out_value, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // NVS_UTILS_H