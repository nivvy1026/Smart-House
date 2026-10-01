#ifndef SECRETS_H
#define SECRETS_H

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_HOST = "ca185ef737c641438f748354507d8a85.s1.eu.hivemq.cloud";
const int   MQTT_PORT = 8883;
const char* MQTT_USER = "esp32";
const char* MQTT_PASS = "EmbeddedSystems2026";
const char* MQTT_CLIENT_ID = "esp32-smarthouse-01";

const char* THINGSPEAK_API_KEY = "DZJXS8I64KHO1LJF";
const char* THINGSPEAK_DOOR_API_KEY = "VN2Q80XQVLMIIS08";

const char* DOOR_PIN        = "1234";
#define TEMP_THRESHOLD_C    35.0
#define DARK_THRESHOLD      1500
#define UNLOCK_HOLD_MS      5000
#define DOORLIGHT_HOLD_MS   10000
#define THINGSPEAK_INTERVAL_MS 30000
#define THINGSPEAK_DOOR_MIN_INTERVAL_MS 60000

#endif