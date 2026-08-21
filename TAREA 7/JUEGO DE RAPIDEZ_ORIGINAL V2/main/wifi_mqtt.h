#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID       "Las Penas"
#define WIFI_PASSWORD   "Pena123321"
#define MQTT_BROKER_URI "mqtt://broker.emqx.io:1883"

typedef void (*mqtt_data_cb_t)(uint32_t reaction1, uint32_t reaction2);

void wifi_mqtt_init(mqtt_data_cb_t cb);
bool wifi_mqtt_is_connected(void);
void wifi_mqtt_publish_times(uint32_t reaction1, uint32_t reaction2);
void wifi_mqtt_publish_status(const char *msg);

#ifdef __cplusplus
}
#endif

#endif