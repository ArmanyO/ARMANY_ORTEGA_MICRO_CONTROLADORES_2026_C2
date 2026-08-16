# MQTT5 LED Control - ESP32

Control del LED integrado (GPIO 2) vía MQTT v5.

## Configuración (ya incluida en platformio.ini)
- WiFi: **Las Penas** / **Pena123321**
- Broker: **broker.hivemq.com** (público)
- Topic: **/topic/led**

## Uso en VS Code + PlatformIO

1. **Abrir carpeta** en VS Code
2. **Compilar**: `Ctrl+Shift+P` → `PlatformIO: Build`
3. **Subir**: `Ctrl+Shift+P` → `PlatformIO: Upload`
4. **Monitor**: `Ctrl+Shift+P` → `PlatformIO: Monitor`

## Probar LED

```bash
# Encender
mosquitto_pub -h broker.hivemq.com -t "/topic/led" -m "ON"

# Apagar
mosquitto_pub -h broker.hivemq.com -t "/topic/led" -m "OFF"

# O con 1/0
mosquitto_pub -h broker.hivemq.com -t "/topic/led" -m "1"
mosquitto_pub -h broker.hivemq.com -t "/topic/led" -m "0"
```

## Cambiar WiFi/Broker
`Ctrl+Shift+P` → `PlatformIO: Menuconfig` → **MQTT5 LED Config**