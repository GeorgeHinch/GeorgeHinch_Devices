#pragma once

#include <Arduino.h>

namespace DeviceSerialSetup {

constexpr size_t COMMAND_BUFFER_SIZE = 160;
constexpr uint32_t COMMAND_TIMEOUT_MS = 3000;
inline char commandBuffer[COMMAND_BUFFER_SIZE + 1];
inline size_t commandLength = 0;
inline uint32_t lastByteMs = 0;

inline bool readArgument(const String& raw, const String& upper,
                         const char* prefix, String& value) {
  const String key(prefix);
  if (!upper.startsWith(key) || upper.length() <= key.length() ||
      upper.charAt(key.length()) != ' ') {
    return false;
  }
  value = raw.substring(key.length() + 1);
  value.trim();
  return true;
}

inline void printHelp() {
  Serial.println("Setup commands:");
  Serial.println("  AP | PORTAL | SETUP");
  Serial.println("  WIFI SSID <network name>");
  Serial.println("  WIFI PASSWORD <password>");
  Serial.println("  WIFI STATUS");
  Serial.println("  WIFI SAVE");
  Serial.println("  WIFI CLEAR");
}

template <typename SaveFunction, typename PortalFunction>
inline void process(const String& rawCommand, char* wifiSsid, size_t ssidSize,
                    char* wifiPass, size_t passSize, SaveFunction saveSettings,
                    PortalFunction startPortal) {
  String raw = rawCommand;
  raw.trim();
  if (raw.isEmpty()) return;
  String command = raw;
  command.toUpperCase();

  if (command == "HELP" || command == "WIFI HELP") {
    printHelp();
    return;
  }
  if (command == "AP" || command == "PORTAL" || command == "SETUP") {
    Serial.println("Starting configuration access point.");
    startPortal();
    return;
  }
  if (command == "WIFI STATUS") {
    Serial.printf("Wi-Fi SSID: %s\n", strlen(wifiSsid) ? wifiSsid : "(not set)");
    Serial.printf("Wi-Fi password: %s\n", strlen(wifiPass) ? "set" : "(not set)");
    Serial.printf("Wi-Fi connection: %s\n",
                  WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
    return;
  }
  if (command == "WIFI SAVE") {
    if (!strlen(wifiSsid)) {
      Serial.println("Wi-Fi save failed: enter an SSID first.");
      return;
    }
    saveSettings();
    Serial.println("Wi-Fi settings saved. Restarting.");
    delay(300);
    ESP.restart();
    return;
  }
  if (command == "WIFI CLEAR") {
    wifiSsid[0] = '\0';
    wifiPass[0] = '\0';
    saveSettings();
    Serial.println("Wi-Fi settings cleared. Restarting into setup mode.");
    delay(300);
    ESP.restart();
    return;
  }

  String value;
  if (readArgument(raw, command, "WIFI SSID", value) ||
      readArgument(raw, command, "SSID", value)) {
    if (value.isEmpty()) {
      Serial.println("SSID cannot be empty.");
      return;
    }
    strlcpy(wifiSsid, value.c_str(), ssidSize);
    Serial.printf("Wi-Fi SSID set to '%s'. Use WIFI SAVE to store it.\n", wifiSsid);
    return;
  }
  if (readArgument(raw, command, "WIFI PASSWORD", value) ||
      readArgument(raw, command, "WIFI PASS", value) ||
      readArgument(raw, command, "PASSWORD", value) ||
      readArgument(raw, command, "PASS", value)) {
    strlcpy(wifiPass, value.c_str(), passSize);
    Serial.println("Wi-Fi password set. Use WIFI SAVE to store it.");
    return;
  }

  Serial.printf("Unknown setup command: %s\n", raw.c_str());
  printHelp();
}

template <typename SaveFunction, typename PortalFunction>
inline void service(char* wifiSsid, size_t ssidSize, char* wifiPass,
                    size_t passSize, SaveFunction saveSettings,
                    PortalFunction startPortal) {
  while (Serial.available() > 0) {
    char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value == '\n') {
      commandBuffer[commandLength] = '\0';
      process(String(commandBuffer), wifiSsid, ssidSize, wifiPass, passSize,
              saveSettings, startPortal);
      commandLength = 0;
      lastByteMs = millis();
      continue;
    }

    if (commandLength == 0) lastByteMs = millis();
    if (commandLength < COMMAND_BUFFER_SIZE) commandBuffer[commandLength++] = value;
    lastByteMs = millis();
  }

  if (commandLength > 0 && millis() - lastByteMs > COMMAND_TIMEOUT_MS) {
    commandLength = 0;
    Serial.println("Serial command timed out. Type HELP and press Enter.");
  }
}

}  // namespace DeviceSerialSetup

