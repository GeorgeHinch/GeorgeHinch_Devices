#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#ifndef DEVICE_TYPE
#error "DEVICE_TYPE must be defined before including DeviceOta.h"
#endif
#ifndef HARDWARE_TARGET
#error "HARDWARE_TARGET must be defined before including DeviceOta.h"
#endif
#ifndef HARDWARE_REVISION
#error "HARDWARE_REVISION must be defined before including DeviceOta.h"
#endif

namespace DeviceOta {

constexpr uint32_t CHECK_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t CHECK_BOOT_DELAY_MS = 2000;
constexpr size_t MANIFEST_DOC_SIZE = 4096;

inline String commandTopic;
inline String stateTopic;
inline bool updateQueued = false;
inline String queuedVersion;
inline String queuedUrl;
inline String queuedSha;
inline size_t queuedSize = 0;
inline String lastState = "Not checked";
inline uint32_t lastCheckMs = 0;
inline bool bootCheckDone = false;

extern "C" {
  extern const uint8_t _binary_x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
  extern const uint8_t _binary_x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
}

inline void configureCaBundle(WiFiClientSecure& client) {
  const size_t bundleSize = static_cast<size_t>(
      _binary_x509_crt_bundle_end - _binary_x509_crt_bundle_start);
  client.setCACertBundle(_binary_x509_crt_bundle_start, bundleSize);
}

inline void begin(const char* mqttPrefix, const char* deviceId) {
  String base = mqttPrefix;
  if (!base.endsWith("/")) base += "/";
  base += deviceId;
  commandTopic = base + "/ota/cmd";
  stateTopic = base + "/ota/state";
}

inline void publishState(PubSubClient& mqtt, bool mqttEnabled, const String& value) {
  lastState = value;
  if (mqttEnabled && mqtt.connected() && !stateTopic.isEmpty()) {
    mqtt.publish(stateTopic.c_str(), value.c_str(), true);
  }
}

inline String manifestUrl() {
  return String("https://github.com/") + OTA_REPOSITORY_OWNER + "/" +
         OTA_REPOSITORY_NAME + "/releases/latest/download/manifest.json";
}

inline String normalizeVersion(const String& value) {
  if (!value.isEmpty() && (value[0] == 'v' || value[0] == 'V')) return value.substring(1);
  return value;
}

inline int compareVersions(const String& leftValue, const String& rightValue) {
  String left = normalizeVersion(leftValue);
  String right = normalizeVersion(rightValue);
  int leftIndex = 0;
  int rightIndex = 0;
  for (int component = 0; component < 3; ++component) {
    int leftDot = left.indexOf('.', leftIndex);
    int rightDot = right.indexOf('.', rightIndex);
    int leftPart = left.substring(leftIndex, leftDot < 0 ? left.length() : leftDot).toInt();
    int rightPart = right.substring(rightIndex, rightDot < 0 ? right.length() : rightDot).toInt();
    if (leftPart != rightPart) return leftPart < rightPart ? -1 : 1;
    leftIndex = leftDot < 0 ? left.length() : leftDot + 1;
    rightIndex = rightDot < 0 ? right.length() : rightDot + 1;
  }
  return 0;
}

inline String bytesToHex(const unsigned char* bytes, size_t length) {
  static const char* digits = "0123456789abcdef";
  String output;
  output.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    output += digits[(bytes[index] >> 4) & 0x0F];
    output += digits[bytes[index] & 0x0F];
  }
  return output;
}

inline bool downloadAndInstall(PubSubClient& mqtt, bool mqttEnabled,
                               const String& url, const String& expectedSha256,
                               size_t expectedSize) {
  WiFiClientSecure client;
  configureCaBundle(client);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    publishState(mqtt, mqttEnabled, "Update failed: request initialization");
    return false;
  }

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    publishState(mqtt, mqttEnabled, String("Update failed: firmware HTTP ") + status);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (expectedSize > 0 && contentLength > 0 &&
      static_cast<size_t>(contentLength) != expectedSize) {
    publishState(mqtt, mqttEnabled, "Update failed: firmware size mismatch");
    http.end();
    return false;
  }

  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    Update.printError(Serial);
    publishState(mqtt, mqttEnabled, "Update failed: unable to prepare storage");
    http.end();
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t written = 0;
  while (http.connected() &&
         (contentLength < 0 || written < static_cast<size_t>(contentLength))) {
    size_t available = stream->available();
    if (!available) {
      delay(1);
      continue;
    }

    size_t count = stream->readBytes(buffer, min(available, sizeof(buffer)));
    if (!count) continue;
    mbedtls_sha256_update(&sha, buffer, count);
    if (Update.write(buffer, count) != count) {
      Update.abort();
      mbedtls_sha256_free(&sha);
      publishState(mqtt, mqttEnabled, "Update failed: flash write");
      http.end();
      return false;
    }
    written += count;
  }

  unsigned char digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  http.end();

  String actualSha = bytesToHex(digest, sizeof(digest));
  String wantedSha = expectedSha256;
  actualSha.toLowerCase();
  wantedSha.toLowerCase();
  if (!wantedSha.isEmpty() && actualSha != wantedSha) {
    Update.abort();
    publishState(mqtt, mqttEnabled, "Update failed: verification mismatch");
    return false;
  }

  if (!Update.end(true)) {
    Update.printError(Serial);
    publishState(mqtt, mqttEnabled, "Update failed: finalization");
    return false;
  }

  publishState(mqtt, mqttEnabled, "Updating... restarting");
  delay(500);
  ESP.restart();
  return true;
}

inline void queueUpdate(const String& version, const String& url,
                        const String& sha256, size_t size) {
  queuedVersion = version;
  queuedUrl = url;
  queuedSha = sha256;
  queuedSize = size;
  updateQueued = true;
}

inline void check(PubSubClient& mqtt, bool mqttEnabled, bool safeToInstall,
                  bool force = false) {
  publishState(mqtt, mqttEnabled, "Checking...");
  if (WiFi.status() != WL_CONNECTED) {
    publishState(mqtt, mqttEnabled, "Unable to check: not connected");
    return;
  }

  WiFiClientSecure client;
  configureCaBundle(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, manifestUrl())) {
    publishState(mqtt, mqttEnabled, "Unable to check: request initialization");
    return;
  }

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    publishState(mqtt, mqttEnabled, String("Unable to check: manifest HTTP ") + status);
    http.end();
    return;
  }

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument document;
#else
  DynamicJsonDocument document(MANIFEST_DOC_SIZE);
#endif
  DeserializationError error = deserializeJson(document, http.getStream());
  http.end();
  if (error) {
    publishState(mqtt, mqttEnabled, "Unable to check: invalid manifest");
    return;
  }

  JsonObject entry = document["firmware"][DEVICE_TYPE];
  if (entry.isNull()) {
    publishState(mqtt, mqttEnabled, "Unable to check: firmware not listed");
    return;
  }

  String version = entry["version"] | "";
  String target = entry["target"] | "";
  int minimumHardware = entry["minimum_hardware_revision"] | 1;
  String url = entry["url"] | "";
  String sha256 = entry["sha256"] | "";
  size_t size = entry["size"] | 0;

  if (version.isEmpty() || target.isEmpty() || url.isEmpty() ||
      sha256.isEmpty() || size == 0) {
    publishState(mqtt, mqttEnabled, "Unable to check: incomplete manifest");
    return;
  }
  if (target != HARDWARE_TARGET) {
    publishState(mqtt, mqttEnabled, "Unable to check: incompatible target");
    return;
  }
  if (HARDWARE_REVISION < minimumHardware) {
    publishState(mqtt, mqttEnabled, "Unable to check: incompatible hardware");
    return;
  }
  if (compareVersions(String(FIRMWARE_VERSION), version) >= 0) {
    updateQueued = false;
    publishState(mqtt, mqttEnabled, String("Up to date (v") + FIRMWARE_VERSION + ")");
    return;
  }
  publishState(mqtt, mqttEnabled, String("Update available (v") + version + ")");
  if (!force && !safeToInstall) {
    queueUpdate(version, url, sha256, size);
    return;
  }

  publishState(mqtt, mqttEnabled, String("Updating... (v") + version + ")");
  downloadAndInstall(mqtt, mqttEnabled, url, sha256, size);
}

inline bool handleMqtt(PubSubClient& mqtt, bool mqttEnabled, bool safeToInstall,
                       const String& topic, const String& message) {
  if (topic != commandTopic) return false;
  if (message == "CHECK") check(mqtt, mqttEnabled, safeToInstall, false);
  else if (message == "FORCE") check(mqtt, mqttEnabled, safeToInstall, true);
  else if (message == "STATUS") publishState(mqtt, mqttEnabled, lastState);
  return true;
}

inline void service(PubSubClient& mqtt, bool mqttEnabled, bool safeToInstall) {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED && now >= CHECK_BOOT_DELAY_MS && !bootCheckDone) {
    bootCheckDone = true;
    check(mqtt, mqttEnabled, safeToInstall, false);
    lastCheckMs = now;
  } else if (WiFi.status() == WL_CONNECTED &&
             now - lastCheckMs >= CHECK_INTERVAL_MS) {
    check(mqtt, mqttEnabled, safeToInstall, false);
    lastCheckMs = now;
  }

  if (!updateQueued || !safeToInstall || WiFi.status() != WL_CONNECTED) return;
  publishState(mqtt, mqttEnabled, String("Updating... (v") + queuedVersion + ")");
  if (!downloadAndInstall(mqtt, mqttEnabled, queuedUrl, queuedSha, queuedSize)) {
    updateQueued = false;
    publishState(mqtt, mqttEnabled, "Update failed: queued installation");
  }
}

}  // namespace DeviceOta
