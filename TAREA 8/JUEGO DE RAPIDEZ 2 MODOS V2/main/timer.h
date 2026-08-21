#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_timer.h"

typedef struct {
    esp_timer_handle_t handle;
    bool periodic;
    uint64_t period_us;
    void (*callback)(void *arg);
    void *arg;
} timer_t;

/** @brief Reloj global - "hora actual" en microsegundos desde boot */
static inline uint64_t now_us(void) {
    return esp_timer_get_time();
}

/** @brief Reloj global - "hora actual" en milisegundos desde boot */
static inline uint64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

/** @brief Diferencia de tiempo en microsegundos (maneja wraparound) */
static inline uint64_t elapsed_us(uint64_t start_us) {
    uint64_t now = esp_timer_get_time();
    return (now >= start_us) ? (now - start_us) : (UINT64_MAX - start_us + now + 1);
}

/** @brief Diferencia de tiempo en milisegundos */
static inline uint64_t elapsed_ms(uint64_t start_us) {
    return elapsed_us(start_us) / 1000;
}

static inline void timer_init(timer_t *timer, void (*callback)(void *arg), void *arg) {
    timer->callback = callback;
    timer->arg = arg;
    timer->handle = NULL;
    timer->periodic = false;
    timer->period_us = 0;
}

static inline esp_err_t timer_start_periodic(timer_t *timer, uint64_t period_us) {
    if (timer->handle) return ESP_ERR_INVALID_STATE;
    
    esp_timer_create_args_t args = {
        .callback = timer->callback,
        .arg = timer->arg,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "periodic_timer",
        .skip_unhandled_events = false,
    };
    
    esp_err_t err = esp_timer_create(&args, &timer->handle);
    if (err != ESP_OK) return err;
    
    timer->periodic = true;
    timer->period_us = period_us;
    return esp_timer_start_periodic(timer->handle, period_us);
}

static inline esp_err_t timer_start_once(timer_t *timer, uint64_t timeout_us) {
    if (timer->handle) return ESP_ERR_INVALID_STATE;
    
    esp_timer_create_args_t args = {
        .callback = timer->callback,
        .arg = timer->arg,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "oneshot_timer",
        .skip_unhandled_events = false,
    };
    
    esp_err_t err = esp_timer_create(&args, &timer->handle);
    if (err != ESP_OK) return err;
    
    timer->periodic = false;
    timer->period_us = timeout_us;
    return esp_timer_start_once(timer->handle, timeout_us);
}

static inline esp_err_t timer_stop(timer_t *timer) {
    if (!timer->handle) return ESP_ERR_INVALID_STATE;
    
    esp_err_t err = esp_timer_stop(timer->handle);
    if (err != ESP_OK) return err;
    
    return esp_timer_delete(timer->handle);
}

static inline esp_err_t timer_restart(timer_t *timer, uint64_t new_period_us) {
    esp_err_t err = timer_stop(timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    
    timer->handle = NULL;
    if (timer->periodic) {
        return timer_start_periodic(timer, new_period_us);
    } else {
        return timer_start_once(timer, new_period_us);
    }
}

static inline bool timer_is_running(timer_t *timer) {
    return timer->handle != NULL;
}