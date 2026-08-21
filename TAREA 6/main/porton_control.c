/*
 * Porton automatico con ESP32 - ESP-IDF nativo
 * Control local puro, sin Arduino, sin Wi-Fi ni MQTT en el core.
 *
 * Entradas (pull-up interno, activo en LOW):
 *   - Finales de carrera/pulsadores NO entre GPIO y GND
 *   - Botón NO entre GPIO y GND
 *   - Sensor IR alimentado a 3.3V. Por defecto, obstáculo = LOW
 *
 * Salidas (reles activo en LOW por defecto):
 *   - Rele abrir/cerrar + rele potencia motor
 *   - 8 LEDs de estado
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "porton_control.h"
#include "porton_pins.h"

static const char *TAG = "PORTON_CTRL";

// ------------------------- Temporizaciones -------------------------
#define TIEMPO_ABIERTO_MS            5000ULL
#define TIEMPO_MOSTRAR_ABIERTO_MS    2000ULL
#define TIEMPO_MUERTO_RELES_MS        250ULL
#define TIEMPO_ASENTAR_RELE_MS          50ULL
#define TIEMPO_MAX_MOVIMIENTO_MS    30000ULL
#define ANTIRREBOTE_MS                 40ULL
#define CONFIRMAR_LIBRE_MS             500ULL
#define PARPADEO_ERROR_MS              300ULL
#define TIEMPO_INICIO_MS              1000ULL
#define PERIODO_TAREA_MS                 10

// NVS namespace y keys
#define NVS_NAMESPACE           "porton_cfg"
#define NVS_KEY_OPEN_TIME       "open_time"
#define NVS_KEY_CLOSE_TIME      "close_time"
#define NVS_KEY_AUTO_CLOSE      "auto_close"
#define NVS_KEY_CALIBRATED      "calibrated"

// Valores por defecto
#define DEFAULT_OPEN_TIME_MS     12000ULL
#define DEFAULT_CLOSE_TIME_MS    12000ULL
#define DEFAULT_AUTO_CLOSE_MS     5000ULL

// Callback opcional para notificar estado a WiFi/MQTT
static porton_state_cb_t s_state_callback = NULL;

void porton_control_set_state_callback(porton_state_cb_t cb) {
    s_state_callback = cb;
}

// ------------------------- Variables de configuración -------------------------
static uint32_t g_open_time_ms = DEFAULT_OPEN_TIME_MS;
static uint32_t g_close_time_ms = DEFAULT_CLOSE_TIME_MS;
static uint32_t g_auto_close_ms = DEFAULT_AUTO_CLOSE_MS;
static bool g_calibrated = false;

// Variables de calibración en curso
static bool g_calibrating = false;
static enum { CAL_IDLE, CAL_OPENING, CAL_CLOSING } g_cal_step = CAL_IDLE;
static uint64_t g_cal_start_ms = 0;

// ------------------------- Tipos y variables -------------------------

typedef enum {
    ESTADO_INICIO,
    ESTADO_CERRADO,
    ESTADO_ABRIENDO,
    ESTADO_ABIERTO,
    ESTADO_ESPERANDO_CIERRE,
    ESTADO_CERRANDO,
    ESTADO_OBSTRUIDO,
    ESTADO_ERROR
} estado_porton_t;

typedef enum {
    MOVIMIENTO_NINGUNO,
    MOVIMIENTO_ABRIR,
    MOVIMIENTO_CERRAR
} movimiento_t;

typedef struct {
    gpio_num_t pin;
    int nivel_activo;
    int ultima_lectura;
    int valor_estable;
    uint64_t instante_cambio_ms;
    bool flanco_activo;
} entrada_t;

static entrada_t fin_abierto = { .pin = PIN_FIN_ABIERTO, .nivel_activo = NIVEL_FIN_ACTIVO };
static entrada_t fin_cerrado = { .pin = PIN_FIN_CERRADO, .nivel_activo = NIVEL_FIN_ACTIVO };
static entrada_t sensor_ir   = { .pin = PIN_SENSOR_IR,   .nivel_activo = NIVEL_OBSTACULO };
static entrada_t boton       = { .pin = PIN_BOTON,       .nivel_activo = NIVEL_BOTON_ACTIVO };

static estado_porton_t estado_actual = ESTADO_INICIO;
static movimiento_t movimiento_pendiente = MOVIMIENTO_NINGUNO;
static bool estado_inicializado = false;
static bool cerrar_despues_de_abrir = false;
static bool cierre_inicial_pendiente = false;
static bool motor_energizado = false;
static bool esperando_habilitar_motor = false;
static bool led_error_encendido = false;

static uint64_t inicio_espera_ms = 0;
static uint64_t solicitud_movimiento_ms = 0;
static uint64_t rele_configurado_ms = 0;
static uint64_t inicio_movimiento_ms = 0;
static uint64_t inicio_libre_ms = 0;
static uint64_t ultimo_parpadeo_ms = 0;

// ------------------------- Funciones auxiliares -------------------------

static uint64_t ahora_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static bool entrada_activa(const entrada_t *entrada) {
    return entrada->valor_estable == entrada->nivel_activo;
}

static void iniciar_entrada(entrada_t *entrada, uint64_t ahora) {
    entrada->ultima_lectura = gpio_get_level(entrada->pin);
    entrada->valor_estable = entrada->ultima_lectura;
    entrada->instante_cambio_ms = ahora;
    entrada->flanco_activo = false;
}

static void actualizar_entrada(entrada_t *entrada, uint64_t ahora) {
    int lectura = gpio_get_level(entrada->pin);
    if (lectura != entrada->ultima_lectura) {
        entrada->ultima_lectura = lectura;
        entrada->instante_cambio_ms = ahora;
    }
    if ((ahora - entrada->instante_cambio_ms) >= ANTIRREBOTE_MS &&
        entrada->valor_estable != lectura) {
        entrada->valor_estable = lectura;
        entrada->flanco_activo = (lectura == entrada->nivel_activo);
    }
}

static bool consumir_flanco_activo(entrada_t *entrada) {
    bool resultado = entrada->flanco_activo;
    entrada->flanco_activo = false;
    return resultado;
}

static const char *nombre_estado(estado_porton_t estado) {
    switch (estado) {
        case ESTADO_INICIO:           return "INICIO";
        case ESTADO_CERRADO:          return "CERRADO";
        case ESTADO_ABRIENDO:         return "ABRIENDO";
        case ESTADO_ABIERTO:          return "ABIERTO";
        case ESTADO_ESPERANDO_CIERRE: return "ESPERANDO_CIERRE";
        case ESTADO_CERRANDO:         return "CERRANDO";
        case ESTADO_OBSTRUIDO:        return "OBSTRUIDO";
        case ESTADO_ERROR:            return "ERROR";
        default:                      return "DESCONOCIDO";
    }
}

static void escribir_rele(gpio_num_t pin, bool activar) {
    int nivel_activo = RELES_ACTIVOS_EN_LOW ? 0 : 1;
    int nivel_inactivo = RELES_ACTIVOS_EN_LOW ? 1 : 0;
    ESP_ERROR_CHECK(gpio_set_level(pin, activar ? nivel_activo : nivel_inactivo));
}

static void habilitar_potencia_motor(bool habilitar) {
    int nivel_activo = RELE_POTENCIA_ACTIVO_EN_LOW ? 0 : 1;
    int nivel_inactivo = RELE_POTENCIA_ACTIVO_EN_LOW ? 1 : 0;
    ESP_ERROR_CHECK(gpio_set_level(PIN_RELE_POTENCIA, habilitar ? nivel_activo : nivel_inactivo));
}

static void apagar_motor(void) {
    habilitar_potencia_motor(false);
    escribir_rele(PIN_RELE_ABRIR, false);
    escribir_rele(PIN_RELE_CERRAR, false);
    motor_energizado = false;
    esperando_habilitar_motor = false;
    movimiento_pendiente = MOVIMIENTO_NINGUNO;
}

static void actualizar_leds(void) {
    gpio_set_level(PIN_LED_INICIO,            estado_actual == ESTADO_INICIO);
    gpio_set_level(PIN_LED_CERRADO,           estado_actual == ESTADO_CERRADO);
    gpio_set_level(PIN_LED_ABRIENDO,          estado_actual == ESTADO_ABRIENDO);
    gpio_set_level(PIN_LED_ABIERTO,           estado_actual == ESTADO_ABIERTO);
    gpio_set_level(PIN_LED_ESPERANDO_CIERRE,  estado_actual == ESTADO_ESPERANDO_CIERRE);
    gpio_set_level(PIN_LED_CERRANDO,          estado_actual == ESTADO_CERRANDO);
    gpio_set_level(PIN_LED_OBSTRUIDO,         estado_actual == ESTADO_OBSTRUIDO);

    if (estado_actual != ESTADO_ERROR) {
        gpio_set_level(PIN_LED_ERROR, 0);
        led_error_encendido = false;
    }
}

static void notificar_estado(const char *evento) {
    if (s_state_callback) {
        s_state_callback(estado_actual, evento);
    }
}

static void cambiar_estado(estado_porton_t nuevo_estado) {
    if (estado_inicializado && estado_actual == nuevo_estado) return;
    estado_actual = nuevo_estado;
    estado_inicializado = true;
    actualizar_leds();
    ESP_LOGI(TAG, "Estado: %s", nombre_estado(estado_actual));
    notificar_estado("state_change");
}

static void entrar_error(const char *motivo) {
    apagar_motor();
    cerrar_despues_de_abrir = false;
    cierre_inicial_pendiente = false;
    cambiar_estado(ESTADO_ERROR);
    ESP_LOGE(TAG, "%s", motivo);
    notificar_estado(motivo);
}

static void solicitar_motor(movimiento_t movimiento, uint64_t ahora) {
    apagar_motor();
    movimiento_pendiente = movimiento;
    solicitud_movimiento_ms = ahora;
    inicio_movimiento_ms = ahora;
}

static void iniciar_apertura(bool realizar_ciclo, uint64_t ahora) {
    if (entrada_activa(&fin_abierto)) {
        apagar_motor();
        cerrar_despues_de_abrir = realizar_ciclo;
        if (realizar_ciclo) {
            inicio_espera_ms = ahora;
            cambiar_estado(ESTADO_ABIERTO);
        } else {
            cambiar_estado(ESTADO_ABIERTO);
        }
        return;
    }
    cerrar_despues_de_abrir = realizar_ciclo;
    cambiar_estado(ESTADO_ABRIENDO);
    solicitar_motor(MOVIMIENTO_ABRIR, ahora);
}

static void iniciar_cierre(uint64_t ahora) {
    if (entrada_activa(&sensor_ir)) {
        apagar_motor();
        inicio_libre_ms = 0;
        cambiar_estado(ESTADO_OBSTRUIDO);
        return;
    }
    if (entrada_activa(&fin_cerrado)) {
        apagar_motor();
        cerrar_despues_de_abrir = false;
        cambiar_estado(ESTADO_CERRADO);
        return;
    }
    cambiar_estado(ESTADO_CERRANDO);
    solicitar_motor(MOVIMIENTO_CERRAR, ahora);
}

static void atender_movimiento_pendiente(uint64_t ahora) {
    if (esperando_habilitar_motor) {
        if ((ahora - rele_configurado_ms) < TIEMPO_ASENTAR_RELE_MS) return;
        if (estado_actual == ESTADO_ABRIENDO || estado_actual == ESTADO_CERRANDO) {
            habilitar_potencia_motor(true);
            motor_energizado = true;
            inicio_movimiento_ms = ahora;
        } else {
            apagar_motor();
        }
        esperando_habilitar_motor = false;
        return;
    }
    if (movimiento_pendiente == MOVIMIENTO_NINGUNO || motor_energizado) return;
    if ((ahora - solicitud_movimiento_ms) < TIEMPO_MUERTO_RELES_MS) return;

    if (movimiento_pendiente == MOVIMIENTO_ABRIR) {
        if (entrada_activa(&fin_abierto)) {
            apagar_motor();
            inicio_espera_ms = ahora;
            cambiar_estado(ESTADO_ABIERTO);
            return;
        }
        escribir_rele(PIN_RELE_CERRAR, false);
        escribir_rele(PIN_RELE_ABRIR, true);
    } else {
        if (entrada_activa(&fin_cerrado) || entrada_activa(&sensor_ir)) {
            apagar_motor();
            if (entrada_activa(&sensor_ir)) {
                inicio_libre_ms = 0;
                cambiar_estado(ESTADO_OBSTRUIDO);
            } else {
                cerrar_despues_de_abrir = false;
                cambiar_estado(ESTADO_CERRADO);
            }
            return;
        }
        escribir_rele(PIN_RELE_ABRIR, false);
        escribir_rele(PIN_RELE_CERRAR, true);
    }
    movimiento_pendiente = MOVIMIENTO_NINGUNO;
    esperando_habilitar_motor = true;
    rele_configurado_ms = ahora;
}

static void solicitar_ciclo_automatico(uint64_t ahora) {
    switch (estado_actual) {
        case ESTADO_INICIO: break;
        case ESTADO_CERRADO:
            iniciar_apertura(true, ahora);
            break;
        case ESTADO_ABIERTO:
            cerrar_despues_de_abrir = true;
            if (entrada_activa(&sensor_ir)) {
                inicio_libre_ms = 0;
                cambiar_estado(ESTADO_OBSTRUIDO);
            } else {
                inicio_espera_ms = ahora;
                cambiar_estado(ESTADO_ESPERANDO_CIERRE);
            }
            break;
        case ESTADO_ESPERANDO_CIERRE:
            inicio_espera_ms = ahora;
            break;
        case ESTADO_CERRANDO:
            iniciar_apertura(true, ahora);
            break;
        case ESTADO_ABRIENDO:
        case ESTADO_OBSTRUIDO:
        case ESTADO_ERROR:
            break;
    }
}

static void atender_boton(uint64_t ahora) {
    if (!consumir_flanco_activo(&boton)) return;
    ESP_LOGI(TAG, "Botón pulsado");
    solicitar_ciclo_automatico(ahora);
}

static void atender_estado(uint64_t ahora) {
    if (estado_actual != ESTADO_ERROR &&
        entrada_activa(&fin_abierto) && entrada_activa(&fin_cerrado)) {
        entrar_error("Los dos finales de carrera aparecen activos");
        return;
    }

    switch (estado_actual) {
        case ESTADO_ABRIENDO:
            if (entrada_activa(&fin_abierto)) {
                apagar_motor();
                if (cerrar_despues_de_abrir) {
                    inicio_espera_ms = ahora;
                    cambiar_estado(ESTADO_ABIERTO);
                } else {
                    cambiar_estado(ESTADO_ABIERTO);
                }
            } else if ((ahora - inicio_movimiento_ms) >= TIEMPO_MAX_MOVIMIENTO_MS) {
                entrar_error("Tiempo máximo de apertura excedido");
            }
            break;

        case ESTADO_ABIERTO:
            if (cerrar_despues_de_abrir) {
                if (entrada_activa(&sensor_ir)) {
                    inicio_libre_ms = 0;
                    cambiar_estado(ESTADO_OBSTRUIDO);
                } else if ((ahora - inicio_espera_ms) >= TIEMPO_MOSTRAR_ABIERTO_MS) {
                    cambiar_estado(ESTADO_ESPERANDO_CIERRE);
                }
            }
            break;

        case ESTADO_ESPERANDO_CIERRE:
            if (entrada_activa(&sensor_ir)) {
                inicio_libre_ms = 0;
                cambiar_estado(ESTADO_OBSTRUIDO);
            } else if ((ahora - inicio_espera_ms) >= g_auto_close_ms) {
                iniciar_cierre(ahora);
            }
            break;

        case ESTADO_CERRANDO:
            if (entrada_activa(&sensor_ir)) {
                apagar_motor();
                cerrar_despues_de_abrir = true;
                inicio_libre_ms = 0;
                cambiar_estado(ESTADO_OBSTRUIDO);
            } else if (entrada_activa(&fin_cerrado)) {
                apagar_motor();
                cerrar_despues_de_abrir = false;
                cambiar_estado(ESTADO_CERRADO);
            } else if ((ahora - inicio_movimiento_ms) >= TIEMPO_MAX_MOVIMIENTO_MS) {
                entrar_error("Tiempo máximo de cierre excedido");
            }
            break;

        case ESTADO_OBSTRUIDO:
            if (entrada_activa(&sensor_ir)) {
                inicio_libre_ms = 0;
            } else if (inicio_libre_ms == 0) {
                inicio_libre_ms = ahora;
            } else if ((ahora - inicio_libre_ms) >= CONFIRMAR_LIBRE_MS) {
                if (cierre_inicial_pendiente) {
                    cierre_inicial_pendiente = false;
                    iniciar_cierre(ahora);
                } else if (entrada_activa(&fin_abierto)) {
                    inicio_espera_ms = ahora;
                    cambiar_estado(ESTADO_ABIERTO);
                } else {
                    iniciar_apertura(true, ahora);
                }
            }
            break;

        case ESTADO_ERROR:
            if ((ahora - ultimo_parpadeo_ms) >= PARPADEO_ERROR_MS) {
                ultimo_parpadeo_ms = ahora;
                led_error_encendido = !led_error_encendido;
                gpio_set_level(PIN_LED_ERROR, led_error_encendido);
            }
            break;

        case ESTADO_INICIO:
        case ESTADO_CERRADO:
            break;
    }
}

// ------------------------- NVS Config -------------------------

static void nvs_save_uint32(const char *key, uint32_t value) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u32(handle, key, value);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static uint32_t nvs_load_uint32(const char *key, uint32_t default_val) {
    nvs_handle_t handle;
    uint32_t value = default_val;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u32(handle, key, &value);
        nvs_close(handle);
    }
    return value;
}

static void nvs_save_bool(const char *key, bool value) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, key, value ? 1 : 0);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static bool nvs_load_bool(const char *key, bool default_val) {
    nvs_handle_t handle;
    uint8_t value = default_val ? 1 : 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, key, &value);
        nvs_close(handle);
    }
    return value != 0;
}

void porton_control_load_config(void) {
    g_open_time_ms = nvs_load_uint32(NVS_KEY_OPEN_TIME, DEFAULT_OPEN_TIME_MS);
    g_close_time_ms = nvs_load_uint32(NVS_KEY_CLOSE_TIME, DEFAULT_CLOSE_TIME_MS);
    g_auto_close_ms = nvs_load_uint32(NVS_KEY_AUTO_CLOSE, DEFAULT_AUTO_CLOSE_MS);
    g_calibrated = nvs_load_bool(NVS_KEY_CALIBRATED, false);
    ESP_LOGI(TAG, "Config cargada: open=%lu close=%lu auto_close=%lu calibrated=%d",
             g_open_time_ms, g_close_time_ms, g_auto_close_ms, g_calibrated);
}

void porton_control_save_config(void) {
    nvs_save_uint32(NVS_KEY_OPEN_TIME, g_open_time_ms);
    nvs_save_uint32(NVS_KEY_CLOSE_TIME, g_close_time_ms);
    nvs_save_uint32(NVS_KEY_AUTO_CLOSE, g_auto_close_ms);
    nvs_save_bool(NVS_KEY_CALIBRATED, g_calibrated);
    ESP_LOGI(TAG, "Config guardada");
}

// ------------------------- Calibración -------------------------

static void calibration_step(uint64_t ahora) {
    switch (g_cal_step) {
        case CAL_IDLE:
            if (entrada_activa(&fin_cerrado)) {
                ESP_LOGI(TAG, "Calib: iniciando apertura");
                g_cal_step = CAL_OPENING;
                g_cal_start_ms = ahora;
                iniciar_apertura(false, ahora);
            }
            break;

        case CAL_OPENING:
            if (entrada_activa(&fin_abierto)) {
                g_open_time_ms = (uint32_t)(ahora - g_cal_start_ms);
                ESP_LOGI(TAG, "Calib: abierto en %lu ms, iniciando cierre", g_open_time_ms);
                g_cal_step = CAL_CLOSING;
                g_cal_start_ms = ahora;
                iniciar_cierre(ahora);
            } else if ((ahora - g_cal_start_ms) >= TIEMPO_MAX_MOVIMIENTO_MS) {
                ESP_LOGE(TAG, "Calib: timeout apertura");
                g_calibrating = false;
                g_cal_step = CAL_IDLE;
            }
            break;

        case CAL_CLOSING:
            if (entrada_activa(&fin_cerrado)) {
                g_close_time_ms = (uint32_t)(ahora - g_cal_start_ms);
                ESP_LOGI(TAG, "Calib: cerrado en %lu ms", g_close_time_ms);
                g_calibrated = true;
                g_calibrating = false;
                g_cal_step = CAL_IDLE;
                porton_control_save_config();
                notificar_estado("calibrated");
            } else if ((ahora - g_cal_start_ms) >= TIEMPO_MAX_MOVIMIENTO_MS) {
                ESP_LOGE(TAG, "Calib: timeout cierre");
                g_calibrating = false;
                g_cal_step = CAL_IDLE;
            }
            break;
    }
}

void porton_control_start_calibration(void) {
    if (g_calibrating) {
        ESP_LOGW(TAG, "Calibración ya en curso");
        return;
    }
    if (estado_actual != ESTADO_CERRADO) {
        ESP_LOGW(TAG, "Calibración requiere portón en CERRADO");
        return;
    }
    ESP_LOGI(TAG, "Iniciando calibración...");
    g_calibrating = true;
    g_cal_step = CAL_IDLE;
}

bool porton_control_is_calibrated(void) {
    return g_calibrated;
}

uint32_t porton_control_get_open_time_ms(void) {
    return g_open_time_ms;
}

uint32_t porton_control_get_close_time_ms(void) {
    return g_close_time_ms;
}

// ------------------------- Posición estimada -------------------------

int porton_control_get_position_pct(void) {
    if (!g_calibrated) return -1;  // Desconocido

    uint64_t ahora = ahora_ms();
    uint32_t elapsed = 0;

    if (estado_actual == ESTADO_ABRIENDO && motor_energizado) {
        elapsed = (uint32_t)(ahora - inicio_movimiento_ms);
        if (elapsed >= g_open_time_ms) return 100;
        return (elapsed * 100) / g_open_time_ms;
    }
    else if (estado_actual == ESTADO_CERRANDO && motor_energizado) {
        elapsed = (uint32_t)(ahora - inicio_movimiento_ms);
        if (elapsed >= g_close_time_ms) return 0;
        return 100 - ((elapsed * 100) / g_close_time_ms);
    }
    else if (estado_actual == ESTADO_ABIERTO || estado_actual == ESTADO_ESPERANDO_CIERRE) {
        return 100;
    }
    else if (estado_actual == ESTADO_CERRADO) {
        return 0;
    }
    return -1;  // Desconocido (obstruido, error, etc)
}

// ------------------------- Auto-close -------------------------

void porton_control_set_auto_close_ms(uint32_t ms) {
    g_auto_close_ms = ms;
    nvs_save_uint32(NVS_KEY_AUTO_CLOSE, ms);
    ESP_LOGI(TAG, "Auto-close: %lu ms", ms);
}

uint32_t porton_control_get_auto_close_ms(void) {
    return g_auto_close_ms;
}

// ------------------------- API Pública -------------------------

void porton_control_task(void *arg) {
    (void)arg;

    porton_control_load_config();

    uint64_t ahora = ahora_ms();
    iniciar_entrada(&fin_abierto, ahora);
    iniciar_entrada(&fin_cerrado, ahora);
    iniciar_entrada(&sensor_ir, ahora);
    iniciar_entrada(&boton, ahora);

    cambiar_estado(ESTADO_INICIO);
    ESP_LOGI(TAG, "Leyendo entradas durante %llu ms", TIEMPO_INICIO_MS);

    uint64_t inicio_lectura = ahora;
    while ((ahora - inicio_lectura) < TIEMPO_INICIO_MS) {
        ahora = ahora_ms();
        actualizar_entrada(&fin_abierto, ahora);
        actualizar_entrada(&fin_cerrado, ahora);
        actualizar_entrada(&sensor_ir, ahora);
        actualizar_entrada(&boton, ahora);
        vTaskDelay(pdMS_TO_TICKS(PERIODO_TAREA_MS));
    }

    boton.flanco_activo = false;

    if (entrada_activa(&fin_abierto) && entrada_activa(&fin_cerrado)) {
        entrar_error("Estado inicial inválido: ambos finales activos");
    } else if (entrada_activa(&fin_cerrado)) {
        cambiar_estado(ESTADO_CERRADO);
    } else {
        ESP_LOGI(TAG, "Posición inicial no cerrada; buscando final cerrado");
        cierre_inicial_pendiente = true;
        iniciar_cierre(ahora);
        if (estado_actual == ESTADO_CERRANDO) {
            cierre_inicial_pendiente = false;
        }
    }

    ESP_LOGI(TAG, "Control local del portón listo");

    while (true) {
        ahora = ahora_ms();

        actualizar_entrada(&fin_abierto, ahora);
        actualizar_entrada(&fin_cerrado, ahora);
        actualizar_entrada(&sensor_ir, ahora);
        actualizar_entrada(&boton, ahora);

        // Calibración en curso
        if (g_calibrating) {
            calibration_step(ahora);
        }

        atender_boton(ahora);
        atender_estado(ahora);
        atender_movimiento_pendiente(ahora);

        vTaskDelay(pdMS_TO_TICKS(PERIODO_TAREA_MS));
    }
}

void porton_control_init(void) {
    const uint64_t mascara_salidas =
        (1ULL << PIN_RELE_ABRIR) | (1ULL << PIN_RELE_CERRAR) | (1ULL << PIN_RELE_POTENCIA) |
        (1ULL << PIN_LED_CERRADO) | (1ULL << PIN_LED_ABRIENDO) | (1ULL << PIN_LED_ABIERTO) |
        (1ULL << PIN_LED_ESPERANDO_CIERRE) | (1ULL << PIN_LED_CERRANDO) | (1ULL << PIN_LED_INICIO) |
        (1ULL << PIN_LED_OBSTRUIDO) | (1ULL << PIN_LED_ERROR);

    const uint64_t mascara_entradas =
        (1ULL << PIN_FIN_ABIERTO) | (1ULL << PIN_FIN_CERRADO) |
        (1ULL << PIN_SENSOR_IR) | (1ULL << PIN_BOTON);

    // Estado inicial seguro ANTES de configurar GPIOs
    gpio_set_level(PIN_RELE_ABRIR, RELES_ACTIVOS_EN_LOW ? 1 : 0);
    gpio_set_level(PIN_RELE_CERRAR, RELES_ACTIVOS_EN_LOW ? 1 : 0);
    gpio_set_level(PIN_RELE_POTENCIA, RELE_POTENCIA_ACTIVO_EN_LOW ? 1 : 0);

    gpio_config_t salidas = {
        .pin_bit_mask = mascara_salidas,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&salidas));

    gpio_config_t entradas = {
        .pin_bit_mask = mascara_entradas,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&entradas));

    apagar_motor();
    gpio_set_level(PIN_LED_CERRADO, 0);
    gpio_set_level(PIN_LED_ABRIENDO, 0);
    gpio_set_level(PIN_LED_ABIERTO, 0);
    gpio_set_level(PIN_LED_ESPERANDO_CIERRE, 0);
    gpio_set_level(PIN_LED_CERRANDO, 0);
    gpio_set_level(PIN_LED_INICIO, 0);
    gpio_set_level(PIN_LED_OBSTRUIDO, 0);
    gpio_set_level(PIN_LED_ERROR, 0);
}

porton_state_t porton_control_get_state(void) {
    return (porton_state_t)estado_actual;
}

void porton_control_open(void) {
    uint64_t ahora = ahora_ms();
    iniciar_apertura(false, ahora);
}

void porton_control_close(void) {
    uint64_t ahora = ahora_ms();
    iniciar_cierre(ahora);
}

void porton_control_stop(void) {
    apagar_motor();
    if (estado_actual == ESTADO_ABRIENDO || estado_actual == ESTADO_CERRANDO) {
        cambiar_estado(ESTADO_CERRADO);
    }
}

void porton_control_toggle(void) {
    uint64_t ahora = ahora_ms();
    solicitar_ciclo_automatico(ahora);
}