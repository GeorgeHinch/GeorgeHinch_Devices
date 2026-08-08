#pragma once

// Compile-safe defaults. Configure real values through the device portal or
// serial setup commands; do not commit private credentials.
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef MQTT_BROKER
#define MQTT_BROKER ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif
#ifndef JMRI_CHANNEL
#define JMRI_CHANNEL "/trains/"
#endif

constexpr char TOPIC_TURNOUT[] = "turnout/";
constexpr char TOPIC_SENSOR[] = "sensor/";
constexpr char TOPIC_LIGHT[] = "light/";
constexpr char TOPIC_MEMORY[] = "memory/";
constexpr char DISCOVERY_TOPIC[] = "discovery/";
constexpr char NAMES_REQ_TOPIC[] = "names/request/";
constexpr char NAMES_RESP_TOPIC[] = "names/response/";
constexpr char NAMES_PUSH_TOPIC[] = "names/push/";
