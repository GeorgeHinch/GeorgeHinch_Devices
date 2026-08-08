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

constexpr char TOPIC_TURNOUT[] = "track/turnout/";
constexpr char TOPIC_SENSOR[] = "track/sensor/";
constexpr char TOPIC_LIGHT[] = "track/light/";
constexpr char TOPIC_MEMORY[] = "track/memory/";
constexpr char DISCOVERY_TOPIC[] = "track/discovery/";
constexpr char NAMES_REQ_TOPIC[] = "track/names/request/";
constexpr char NAMES_RESP_TOPIC[] = "track/names/response/";
constexpr char NAMES_PUSH_TOPIC[] = "track/names/push/";
