// MQTT broker info (ESP32 will include this)
#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H

const char* MQTT_BROKER = "test.mosquitto.org";
const int   MQTT_PORT   = 1883;
const char* MQTT_TOPIC  = "esp32/ota/update";

#endif
