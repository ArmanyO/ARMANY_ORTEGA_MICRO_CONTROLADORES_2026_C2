#include "adc_utils.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

static const char *TAG = "ADC_UTILS";

static const adc_channel_t gpio_to_adc1_channel[] = {
    [0] = ADC1_CHANNEL_0,   // GPIO 1
    [1] = ADC1_CHANNEL_0,   // GPIO 1
    [2] = ADC1_CHANNEL_1,   // GPIO 2
    [3] = ADC1_CHANNEL_2,   // GPIO 3
    [4] = ADC1_CHANNEL_3,   // GPIO 4
    [5] = ADC1_CHANNEL_4,   // GPIO 5
    [6] = ADC1_CHANNEL_5,   // GPIO 6
    [7] = ADC1_CHANNEL_6,   // GPIO 7
    [8] = ADC1_CHANNEL_7,   // GPIO 8
    [9] = ADC1_CHANNEL_8,   // GPIO 9
    [10] = ADC1_CHANNEL_9,  // GPIO 10
    [11] = ADC1_CHANNEL_3,  // GPIO 11 (not ADC)
    [12] = ADC1_CHANNEL_3,  // GPIO 12 (not ADC)
    [13] = ADC1_CHANNEL_3,  // GPIO 13 (not ADC)
    [14] = ADC1_CHANNEL_6,  // GPIO 14
    [15] = ADC1_CHANNEL_7,  // GPIO 15
    [16] = ADC2_CHANNEL_0,  // GPIO 16
    [17] = ADC2_CHANNEL_1,  // GPIO 17
    [18] = ADC2_CHANNEL_2,  // GPIO 18
    [19] = ADC2_CHANNEL_3,  // GPIO 19
    [20] = ADC2_CHANNEL_4,  // GPIO 20
    [21] = ADC2_CHANNEL_5,  // GPIO 21
    [22] = ADC2_CHANNEL_6,  // GPIO 22
    [23] = ADC2_CHANNEL_7,  // GPIO 23
    [24] = ADC2_CHANNEL_8,  // GPIO 24
    [25] = ADC2_CHANNEL_9,  // GPIO 25
};

static adc_unit_t gpio_to_adc_unit(int gpio_num) {
    if (gpio_num <= 10 || (gpio_num >= 14 && gpio_num <= 15)) {
        return ADC_UNIT_1;
    }
    return ADC_UNIT_2;
}

static adc_channel_t gpio_to_adc_channel(int gpio_num) {
    if (gpio_num <= 10) {
        return gpio_to_adc1_channel[gpio_num];
    } else if (gpio_num >= 14 && gpio_num <= 15) {
        return gpio_to_adc1_channel[gpio_num];
    } else if (gpio_num >= 16 && gpio_num <= 25) {
        return (adc_channel_t)(gpio_num - 16);
    }
    return ADC1_CHANNEL_0;
}

esp_err_t adc_utils_init(void) {
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "ADC1 init failed: %s", esp_err_to_name(ret));
    }

    adc_oneshot_unit_init_cfg_t init_config2 = {
        .unit_id = ADC_UNIT_2,
    };
    ret = adc_oneshot_new_unit(&init_config2, NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "ADC2 init failed: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

bool adc_utils_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;

#if CONFIG_IDF_TARGET_ESP32S3
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
#else
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
#endif

    if (ret == ESP_OK) {
        *out_handle = handle;
        return true;
    }
    return false;
}

int adc_utils_read_raw(int gpio_num) {
    adc_unit_t unit = gpio_to_adc_unit(gpio_num);
    adc_channel_t channel = gpio_to_adc_channel(gpio_num);

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };

    adc_oneshot_unit_handle_t handle = NULL;
    if (unit == ADC_UNIT_1) {
        adc_oneshot_new_unit(&(adc_oneshot_unit_init_cfg_t){.unit_id = ADC_UNIT_1}, &handle);
    } else {
        adc_oneshot_new_unit(&(adc_oneshot_unit_init_cfg_t){.unit_id = ADC_UNIT_2}, &handle);
    }

    if (handle) {
        adc_oneshot_config_channel(handle, channel, &config);
        int raw = 0;
        adc_oneshot_read(handle, channel, &raw);
        return raw;
    }
    return 0;
}

int adc_utils_read_voltage(int gpio_num, adc_cali_handle_t cali_handle) {
    int raw = adc_utils_read_raw(gpio_num);
    if (cali_handle) {
        int voltage = 0;
        adc_cali_raw_to_voltage(cali_handle, raw, &voltage);
        return voltage;
    }
    return (raw * 3300) / 4095;
}