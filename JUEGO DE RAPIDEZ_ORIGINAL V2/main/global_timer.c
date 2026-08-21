#include "global_timer.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "global_timer";
static bool s_initialized = false;

void global_timer_init(void)
{
    if (s_initialized) {
        return;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "Global timer initialized (using esp_timer)");
}

global_time_t global_timer_now_us(void)
{
    return esp_timer_get_time();
}

global_time_t global_timer_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

uint32_t global_timer_elapsed_us(global_time_t start)
{
    global_time_t now = global_timer_now_us();
    return (uint32_t)(now - start);
}

uint32_t global_timer_elapsed_ms(global_time_t start)
{
    return global_timer_elapsed_us(start) / 1000;
}

bool global_timer_expired_us(global_time_t start, uint32_t timeout_us)
{
    return global_timer_elapsed_us(start) >= timeout_us;
}

bool global_timer_expired_ms(global_time_t start, uint32_t timeout_ms)
{
    return global_timer_elapsed_ms(start) >= timeout_ms;
}