#ifndef PORTON_CONTROL_H
#define PORTON_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORTON_STATE_CLOSED       = 0,
    PORTON_STATE_OPEN         = 1,
    PORTON_STATE_OPENING      = 2,
    PORTON_STATE_CLOSING      = 3,
    PORTON_STATE_WAIT_CLOSE   = 4,
    PORTON_STATE_OBSTRUCTED   = 5,
    PORTON_STATE_ERROR        = 6,
    PORTON_STATE_STARTUP      = 7
} porton_state_t;

typedef void (*porton_state_cb_t)(porton_state_t state, const char *event);

// Inicialización y task
void porton_control_init(void);
void porton_control_task(void *arg);
void porton_control_set_state_callback(porton_state_cb_t cb);

// Estado y control básico
porton_state_t porton_control_get_state(void);
void porton_control_open(void);
void porton_control_close(void);
void porton_control_stop(void);
void porton_control_toggle(void);

// ====== NUEVAS FUNCIONES ======

// Posición estimada 0-100% (0=cerrado, 100=abierto)
int porton_control_get_position_pct(void);

// Calibración: mide tiempo real abrir/cerrar
// Llama a esto con el portón cerrado; hará abrir->cerrar y guardará tiempos
void porton_control_start_calibration(void);
bool porton_control_is_calibrated(void);
uint32_t porton_control_get_open_time_ms(void);
uint32_t porton_control_get_close_time_ms(void);

// Auto-close configurable (0 = desactivado)
void porton_control_set_auto_close_ms(uint32_t ms);
uint32_t porton_control_get_auto_close_ms(void);

// Configuración persistente en NVS
void porton_control_save_config(void);
void porton_control_load_config(void);

#ifdef __cplusplus
}
#endif

#endif // PORTON_CONTROL_H