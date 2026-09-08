#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace DeviceMqttEnrollment {

constexpr const char* ENROLLMENT_PATH = "/api/v1/devices/enroll";
constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 5000;

inline bool isManaged(const char* username, const char* topicRoot) {
  return username && topicRoot && strncmp(username, "hm-device-", 10) == 0 &&
         strncmp(topicRoot, "hakomachi/devices/", 19) == 0;
}

inline bool enroll(const char* deviceId, const char* displayName,
                   const char* deviceType, const char* model,
                   const char* firmware, const char* const* capabilities,
                   size_t capabilityCount, char* wifiSsid, size_t wifiSsidSize,
                   char* wifiPassword, size_t wifiPasswordSize,
                   char* broker, size_t brokerSize, uint16_t& port,
                   char* username, size_t usernameSize,
                   char* password, size_t passwordSize, char* topicRoot,
                   size_t topicRootSize) {
  if (WiFi.status() != WL_CONNECTED || !deviceId || !deviceId[0]) return false;

  IPAddress station = WiFi.gatewayIP();
  if (station == IPAddress(0, 0, 0, 0)) station = IPAddress(10, 42, 0, 1);
  const String endpoint = String("http://") + station.toString() + ENROLLMENT_PATH;

  JsonDocument request;
  request["deviceId"] = deviceId;
  request["name"] = displayName ? displayName : deviceId;
  request["deviceType"] = deviceType ? deviceType : "controller";
  request["model"] = model ? model : "";
  request["firmware"] = firmware ? firmware : "";
  request["mac"] = WiFi.macAddress();
  JsonArray list = request["capabilities"].to<JsonArray>();
  for (size_t i = 0; i < capabilityCount; ++i) {
    if (capabilities[i] && capabilities[i][0]) list.add(capabilities[i]);
  }

  String payload;
  serializeJson(request, payload);
  HTTPClient http;
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(RESPONSE_TIMEOUT_MS);
  if (!http.begin(endpoint)) {
    Serial.println(F("HakoMachi enrollment could not open the station endpoint."));
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(payload);
  const String responsePayload = status > 0 ? http.getString() : String();
  http.end();

  if (status == 423) {
    Serial.println(F("HakoMachi enrollment is waiting for an open pairing window."));
    return false;
  }
  if (status != HTTP_CODE_CREATED) {
    Serial.printf("HakoMachi enrollment unavailable (HTTP %d).\n", status);
    return false;
  }

  JsonDocument response;
  if (deserializeJson(response, responsePayload)) {
    Serial.println(F("HakoMachi enrollment returned invalid JSON."));
    return false;
  }

  const char* responseDeviceId = response["deviceId"] | "";
  const char* responseWifiSsid = response["wifiSsid"] | "";
  const char* responseWifiPassword = response["wifiPassword"] | "";
  const char* responseBroker = response["broker"] | "";
  const char* responseUsername = response["username"] | "";
  const char* responsePassword = response["password"] | "";
  const char* responseTopicRoot = response["topicRoot"] | "";
  const int responsePort = response["port"] | 0;
  const String expectedTopicRoot = String("hakomachi/devices/") + deviceId;
  if (strcmp(responseDeviceId, deviceId) != 0 || responsePort < 1 ||
      responsePort > 65535 || expectedTopicRoot != responseTopicRoot ||
      !responseWifiSsid[0] || strlen(responseWifiSsid) >= wifiSsidSize ||
      strlen(responseWifiPassword) >= wifiPasswordSize ||
      !responseBroker[0] || strlen(responseBroker) >= brokerSize ||
      !responseUsername[0] || strlen(responseUsername) >= usernameSize ||
      !responsePassword[0] || strlen(responsePassword) >= passwordSize ||
      !responseTopicRoot[0] || strlen(responseTopicRoot) >= topicRootSize) {
    Serial.println(F("HakoMachi enrollment response failed validation."));
    return false;
  }

  strlcpy(wifiSsid, responseWifiSsid, wifiSsidSize);
  strlcpy(wifiPassword, responseWifiPassword, wifiPasswordSize);
  strlcpy(broker, responseBroker, brokerSize);
  strlcpy(username, responseUsername, usernameSize);
  strlcpy(password, responsePassword, passwordSize);
  strlcpy(topicRoot, responseTopicRoot, topicRootSize);
  port = static_cast<uint16_t>(responsePort);
  Serial.printf("HakoMachi enrollment complete: Wi-Fi %s, MQTT %s:%u (%s)\n",
                wifiSsid, broker, port, topicRoot);
  return true;
}

}  // namespace DeviceMqttEnrollment
