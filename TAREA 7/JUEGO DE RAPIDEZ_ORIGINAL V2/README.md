# Juego de Rapidez - ESP32 (ESP-IDF)

Juego de tiempo de reacción para dos jugadores implementado en **ESP-IDF nativo**.

## Hardware
| Componente | Pin GPIO |
|------------|----------|
| Botón 1 (Jugador 1) | GPIO 4  |
| Botón 2 (Jugador 2) | GPIO 13 |
| LED 1 (Integrado)   | GPIO 2  |
| LED 2 (Externo)     | GPIO 15 |

> Botones: active LOW con pull-up interno.

## Flujo del juego
1. **Esperar inicio** - LEDs apagados
2. **Presionar Botón 1** - Inicia cuenta regresiva aleatoria (1.5–4.5s)
3. **LED 1 se enciende** - Soltar Botón 1 lo más rápido posible → **Reacción 1**
4. **Presionar Botón 2** - Lo más rápido tras soltar Botón 1 → **Reacción 2**
5. **Resultados** - LED 2 parpadea 2s, se publican por MQTT

## Arquitectura
```
main/
├── main.c              # app_main() + bucle principal
├── global_timer.*      # 🕐 Timer global (esp_timer, 64-bit µs)
├── game_logic.*        # Máquina de estados del juego
├── gpio_handler.*      # GPIO + debounce (2ms)
└── wifi_mqtt.*         # WiFi + MQTT (esp-mqtt nativo)
```

**Principio clave**: Todo módulo pide la hora al **timer global** (`global_timer_now_us()`).

## Compilar y flashear
```bash
# Configurar target
idf.py set-target esp32

# Compilar
idf.py build

# Flashear + monitor (ajusta puerto)
idf.py -p COM5 flash monitor
```

## MQTT
- Broker: `broker.emqx.io:1883` (público, sin auth)
- Topics:
  - `reaction_game/times` → JSON: `{"reaction1_ms":123,"reaction2_ms":456}`
  - `reaction_game/control` → Suscrito (para futuras extensiones)
  - `topic/qos0` → Mensajes de estado legibles

## Configuración
Editar `main/wifi_mqtt.h`:
```c
#define WIFI_SSID       "TuRed"
#define WIFI_PASSWORD   "TuPass"
#define MQTT_BROKER_URI "mqtt://broker.emqx.io:1883"
```

## Requisitos
- ESP-IDF v5.x
- ESP32 DevKit (o compatible)
- Python 3.8+

## Licencia
MIT - Uso educativo.