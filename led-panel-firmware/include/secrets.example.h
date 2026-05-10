#pragma once

// Copy this file to secrets.h and fill in real values.
// secrets.h is gitignored.

#define WIFI_SSID       "your-ssid"
#define WIFI_PASSWORD   "your-password"

// LAN IP of the led-notifier server running Mosquitto
#define MQTT_HOST       "192.168.1.x"
#define MQTT_PORT       1883
#define MQTT_TOPIC      "led/command"
#define MQTT_CLIENT_ID  "led-panel-1"
