#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "sdkconfig.h"

#if CONFIG_ESP_WIFI_REMOTE_ENABLED
#error "WPS provisioning requires native ESP32 Wi-Fi"
#endif

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_wps.h"

namespace DeviceWifiProvisioning {

constexpr uint32_t WPS_PAIRING_WINDOW_MS = 125000;
constexpr uint32_t WPS_CONNECT_TIMEOUT_MS = 15000;

enum class WpsState : uint8_t { Idle, Running, Success, Failed, TimedOut };
inline volatile WpsState wpsState = WpsState::Idle;

inline void onWiFiEvent(WiFiEvent_t event, arduino_event_info_t) {
  switch (event) {
    case ARDUINO_EVENT_WPS_ER_SUCCESS: wpsState = WpsState::Success; break;
    case ARDUINO_EVENT_WPS_ER_FAILED: wpsState = WpsState::Failed; break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT: wpsState = WpsState::TimedOut; break;
    default: break;
  }
}

inline void configureStation(const char* deviceName) {
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  if (deviceName && deviceName[0]) WiFi.setHostname(deviceName);
  WiFi.mode(WIFI_STA);
}

inline void stopWps() {
  const esp_err_t result = esp_wifi_wps_disable();
  if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_INIT) {
    Serial.printf("WPS disable failed: %s\n", esp_err_to_name(result));
  }
}

inline bool beginWps(const char* deviceName) {
  configureStation(deviceName);
  WiFi.onEvent(onWiFiEvent);
  esp_wps_config_t config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
  snprintf(config.factory_info.manufacturer, sizeof(config.factory_info.manufacturer), "HakoMachi");
  snprintf(config.factory_info.model_number, sizeof(config.factory_info.model_number), "ESP32-C3");
  snprintf(config.factory_info.model_name, sizeof(config.factory_info.model_name), "Model train controller");
  snprintf(config.factory_info.device_name, sizeof(config.factory_info.device_name), "%s",
           deviceName && deviceName[0] ? deviceName : "HakoMachi device");
  esp_err_t result = esp_wifi_wps_enable(&config);
  if (result != ESP_OK) {
    Serial.printf("WPS enable failed: %s\n", esp_err_to_name(result));
    return false;
  }
  wpsState = WpsState::Running;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
  result = esp_wifi_wps_start();
#else
  result = esp_wifi_wps_start(0);
#endif
  if (result != ESP_OK) {
    Serial.printf("WPS start failed: %s\n", esp_err_to_name(result));
    stopWps();
    wpsState = WpsState::Failed;
    return false;
  }
  return true;
}

inline bool provision(char* ssid, size_t ssidSize, char* password, size_t passwordSize,
                      const char* deviceName) {
  Serial.println(F("No Wi-Fi credentials saved. Waiting for HakoMachi router pairing..."));
  if (!beginWps(deviceName)) return false;
  const uint32_t pairingStarted = millis();
  while (wpsState == WpsState::Running && millis() - pairingStarted < WPS_PAIRING_WINDOW_MS) delay(25);
  if (wpsState != WpsState::Success) {
    if (wpsState == WpsState::Running) wpsState = WpsState::TimedOut;
    stopWps();
    Serial.println(F("Router pairing was not completed; opening the local setup portal."));
    return false;
  }
  stopWps();
  delay(10);
  WiFi.begin();
  const uint32_t connectStarted = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - connectStarted < WPS_CONNECT_TIMEOUT_MS) delay(50);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WPS supplied credentials, but the Wi-Fi connection did not complete."));
    return false;
  }
  strlcpy(ssid, WiFi.SSID().c_str(), ssidSize);
  strlcpy(password, WiFi.psk().c_str(), passwordSize);
  Serial.printf("Router pairing complete: %s (%s)\n", ssid, WiFi.localIP().toString().c_str());
  return ssid[0] != '\0';
}

inline void forgetStoredNetwork() { WiFi.disconnect(false, true); }

}  // namespace DeviceWifiProvisioning
