# PORTON

Proyecto PlatformIO para ESP32-D0WD-V3 / `esp32dev`.

## Hardware usado

- Boton 1: `GPIO13` / `T4`, touch capacitivo. Mantienes el dedo aqui y lo quitas para medir la primera reaccion.
- LED 1: `GPIO2`, LED integrado en muchas placas ESP32.
- Boton 2: `GPIO4` / `T0`, touch capacitivo tocando el pin con el dedo.
- LED 2: `GPIO15`, configurado como salida.

> Nota: `GPIO15` no trae LED integrado en muchas placas ESP32 DevKit. El firmware lo conmuta igual, pero solo se vera si tu placa concreta tiene un LED conectado a ese pin.
> Nota: `GPIO13` es touch (`T4`) y es mas seguro que `GPIO12` para este uso porque no es pin de arranque/strapping.

## Flujo

1. Abre el monitor serie a `115200`.
2. Al arrancar, deja `GPIO13` y `GPIO4` sin tocar durante la calibracion de 2 s.
3. Manten el dedo en `GPIO13/T4`.
4. Tras un delay aleatorio de 1 a 8 s se enciende `LED1/GPIO2`.
5. Quita el dedo de `GPIO13/T4`: se mide `reaction1_ms`.
6. Toca `GPIO4/T0`: se mide `reaction2_ms` en el primer cruce de umbral y `LED2/GPIO15` enciende inmediatamente.
7. Publica por MQTT:

```json
{"reaction1_ms":123,"reaction2_ms":456}
```

Broker: `broker.emqx.io:1883`  
Topico: `reaction_game/times`

## Comandos utiles

```powershell
pio run
pio run -t upload
pio device monitor
```

El puerto esta fijado en `COM5` en `platformio.ini`.

## Calibracion touch

En el monitor serie:

- `c`: recalibra el baseline de touch, solo en reposo/cooldown.
- `1`: sube el umbral de `GPIO13/T4`.
- `2`: baja el umbral de `GPIO13/T4`.
- `+`: sube el umbral de `GPIO4/T0`.
- `-`: baja el umbral de `GPIO4/T0`.
- `r`: resetea la ronda.

En ESP32 clasico, `touchRead()` normalmente baja cuando tocas el pin. El umbral inicial se fija a `70%` del baseline sin tocar.
