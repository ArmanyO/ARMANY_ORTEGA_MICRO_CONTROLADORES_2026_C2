

#ifndef PORTON_PINS_H
#define PORTON_PINS_H

#include "driver/gpio.h"

// ==================== RELES MOTOR (salidas) ====================
// Puente H o 2 relés: uno para abrir, otro para cerrar
#define PIN_RELE_ABRIR       GPIO_NUM_16  // Rele dirección ABRIR
#define PIN_RELE_CERRAR      GPIO_NUM_17  // Rele dirección CERRAR
#define PIN_RELE_POTENCIA    GPIO_NUM_4   // Rele habilita potencia motor (seguridad)

// ==================== ENTRADAS (pull-up interno) ====================
// Finales de carrera / pulsadores NO entre GPIO y GND
// Activo = LOW (con pull-up), Libre = HIGH
#define PIN_FIN_ABIERTO      GPIO_NUM_18  // Final carrera ABIERTO
#define PIN_FIN_CERRADO      GPIO_NUM_19  // Final carrera CERRADO
#define PIN_SENSOR_IR        GPIO_NUM_21  // Sensor IR obstáculo (LOW = detecta)
#define PIN_BOTON            GPIO_NUM_22  // Pulsador usuario (LOW = pulsado)

// ==================== LEDs ESTADO (salidas) ====================
// 8 LEDs: uno por estado principal
#define PIN_LED_CERRADO          GPIO_NUM_23   // Estado: CERRADO
#define PIN_LED_ABRIENDO         GPIO_NUM_25   // Estado: ABRIENDO
#define PIN_LED_ABIERTO          GPIO_NUM_26   // Estado: ABIERTO
#define PIN_LED_ESPERANDO_CIERRE GPIO_NUM_27   // Estado: ESPERANDO_CIERRE (abierto, cuenta 5s)
#define PIN_LED_CERRANDO         GPIO_NUM_32   // Estado: CERRANDO
#define PIN_LED_INICIO           GPIO_NUM_33   // Estado: INICIO (arranque)
#define PIN_LED_OBSTRUIDO        GPIO_NUM_13   // Estado: OBSTRUIDO (sensor IR)
#define PIN_LED_ERROR            GPIO_NUM_14   // Estado: ERROR (parpadea)

// ==================== POLARIDAD RELES ====================
// 1 = activo en LOW (relé típico optoacoplado), 0 = activo en HIGH
#define RELES_ACTIVOS_EN_LOW        1
#define RELE_POTENCIA_ACTIVO_EN_LOW 1

// ==================== NIVELES ACTIVOS SENSORES ====================
// 0 = LOW activo (con pull-up), 1 = HIGH activo
#define NIVEL_OBSTACULO             0  // Sensor IR: LOW = obstáculo
#define NIVEL_FIN_ACTIVO            0  // Finales carrera: LOW = accionado
#define NIVEL_BOTON_ACTIVO          0  // Botón: LOW = pulsado

// ==================== VALIDACIÓN COMPILE-TIME ====================
// Verificar que no hay pines duplicados
#if (PIN_RELE_ABRIR == PIN_RELE_CERRAR) || \
    (PIN_RELE_ABRIR == PIN_RELE_POTENCIA) || \
    (PIN_RELE_CERRAR == PIN_RELE_POTENCIA)
#error "Pines de reles duplicados"
#endif

#if (PIN_FIN_ABIERTO == PIN_FIN_CERRADO) || \
    (PIN_FIN_ABIERTO == PIN_SENSOR_IR) || \
    (PIN_FIN_ABIERTO == PIN_BOTON) || \
    (PIN_FIN_CERRADO == PIN_SENSOR_IR) || \
    (PIN_FIN_CERRADO == PIN_BOTON) || \
    (PIN_SENSOR_IR == PIN_BOTON)
#error "Pines de entradas duplicados"
#endif

// LEDs únicos
#define _LED_LIST \
    PIN_LED_CERRADO, PIN_LED_ABRIENDO, PIN_LED_ABIERTO, PIN_LED_ESPERANDO_CIERRE, \
    PIN_LED_CERRANDO, PIN_LED_INICIO, PIN_LED_OBSTRUIDO, PIN_LED_ERROR

// No validar duplicados en LEDs por simplicidad, pero evítalos

#endif // PORTON_PINS_H