#include "porton_control.h"
#include "wifi_mqtt.h"

extern "C" void app_main(void) {
    // Inicializar hardware del portón
    porton_control_init();

    // Crear task de control local (prioridad alta, core 0 o 1)
    xTaskCreatePinnedToCore(porton_control_task, "porton_ctrl", 4096, NULL, 10, NULL, 1);

    // Iniciar WiFi/MQTT opcional (comenta si no quieres red)
    wifi_mqtt_start();

    // app_main termina, las tasks corren en background
}