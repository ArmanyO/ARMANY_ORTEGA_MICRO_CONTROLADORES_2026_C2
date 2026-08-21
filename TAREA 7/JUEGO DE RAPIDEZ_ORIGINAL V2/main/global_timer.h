#ifndef GLOBAL_TIMER_H
#define GLOBAL_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t global_time_t;

void global_timer_init(void);
global_time_t global_timer_now_us(void);
global_time_t global_timer_now_ms(void);
uint32_t global_timer_elapsed_us(global_time_t start);
uint32_t global_timer_elapsed_ms(global_time_t start);
bool global_timer_expired_us(global_time_t start, uint32_t timeout_us);
bool global_timer_expired_ms(global_time_t start, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif