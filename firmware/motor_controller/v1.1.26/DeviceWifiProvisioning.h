#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace DeviceWifiProvisioning {

constexpr const char* SETUP_SSID = "HakoMachi-Setup";
constexpr const char* SETUP_PASSWORD = "HakoMachi-Setup-v1";
constexpr uint32_t SETUP_NETWORK_WINDOW_MS = 125000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;

inline void configureStation(const char* deviceName) {
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  if (deviceName && deviceName[0]) WiFi.setHostname(deviceName);
  WiFi.mode(WIFI_STA);
}

inline bool isSetupNetwork() {
  return WiFi.status() == WL_CONNECTED && WiFi.SSID() == SETUP_SSID;
}

inline bool provision(const char* deviceName) {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  if (deviceName && deviceName[0]) WiFi.setHostname(deviceName);
  WiFi.mode(WIFI_STA);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("Wi-Fi disconnected (reason %u).\n",
                  info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  Serial.printf("Waiting for zero-config network '%s'...\n", SETUP_SSID);

  const int networkCount = WiFi.scanNetworks();
  bool setupFound = false;
  int32_t setupChannel = 0;
  if (networkCount < 0) {
    Serial.printf("Wi-Fi scan failed (%d).\n", networkCount);
  } else {
    Serial.printf("Wi-Fi scan found %d network(s).\n", networkCount);
    for (int index = 0; index < networkCount; ++index) {
      if (WiFi.SSID(index) == SETUP_SSID) {
        setupFound = true;
        setupChannel = WiFi.channel(index);
        Serial.printf("  %s found on channel %ld (%d dBm).\n", SETUP_SSID,
                      static_cast<long>(setupChannel), WiFi.RSSI(index));
      }
    }
  }
  Serial.printf("%s was %s in the scan.\n", SETUP_SSID,
                setupFound ? "found" : "not found");
  WiFi.scanDelete();

  const uint32_t started = millis();
  uint32_t lastAttempt = 0;
  while (millis() - started < SETUP_NETWORK_WINDOW_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      if (isSetupNetwork()) {
        Serial.printf("Zero-config network connected: %s\n",
                      WiFi.localIP().toString().c_str());
        return true;
      }
      WiFi.disconnect(false, false);
    }
    if (lastAttempt == 0 || millis() - lastAttempt >= WIFI_CONNECT_TIMEOUT_MS) {
      WiFi.disconnect(true, false);
      delay(250);
      WiFi.persistent(false);
      WiFi.setAutoReconnect(false);
      WiFi.setSleep(false);
      if (deviceName && deviceName[0]) WiFi.setHostname(deviceName);
      WiFi.mode(WIFI_STA);
      const wl_status_t result = WiFi.begin(SETUP_SSID, SETUP_PASSWORD);
      Serial.printf("Connecting to %s (status %d)...\n", SETUP_SSID,
                    static_cast<int>(result));
      lastAttempt = millis();
    }
    delay(100);
  }

  WiFi.disconnect(false, false);
  Serial.println(F("HakoMachi-Setup was not available; opening the local setup portal."));
  return false;
}

inline void forgetStoredNetwork() {
  WiFi.disconnect(false, true);
}

}  // namespace DeviceWifiProvisioning
