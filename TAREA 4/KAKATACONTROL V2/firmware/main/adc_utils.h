#ifndef ADC_UTILS_H
#define ADC_UTILS_H

#include <stdint.h>
#include "esp_adc_cal.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t adc_utils_init(void);
bool adc_utils_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);
int adc_utils_read_raw(int gpio_num);
int adc_utils_read_voltage(int gpio_num, adc_cali_handle_t cali_handle);

#ifdef __cplusplus
}
#endif

#endif // ADC_UTILS_H