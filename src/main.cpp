#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "config.h"

#ifndef DEVICE_TYPE
#define DEVICE_TYPE "crossing-controller"
#endif
#ifndef HARDWARE_TARGET
#define HARDWARE_TARGET "esp32-c3"
#endif
#ifndef HARDWARE_REVISION
#define HARDWARE_REVISION 1
#endif
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.0.0"
#endif
#ifndef OTA_REPOSITORY_OWNER
#define OTA_REPOSITORY_OWNER "GeorgeHinch"
#endif
#ifndef OTA_REPOSITORY_NAME
#define OTA_REPOSITORY_NAME "esp32-github-ota"
#endif

namespace {
constexpr uint32_t CHECK_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
uint32_t lastCheck = 0;

String manifestUrl() {
  return String("https://github.com/") + OTA_REPOSITORY_OWNER + "/" +
         OTA_REPOSITORY_NAME + "/releases/latest/download/manifest.json";
}

bool isValidConfiguredCertificate() {
  return String(GITHUB_ROOT_CA).indexOf("REPLACE_WITH_CURRENT") < 0;
}

int compareSemver(const String &left, const String &right) {
  int li = 0;
  int ri = 0;
  for (int component = 0; component < 3; ++component) {
    int ldot = left.indexOf('.', li);
    int rdot = right.indexOf('.', ri);
    String lp = left.substring(li, ldot < 0 ? left.length() : ldot);
    String rp = right.substring(ri, rdot < 0 ? right.length() : rdot);
    int lv = lp.toInt();
    int rv = rp.toInt();
    if (lv != rv) return lv < rv ? -1 : 1;
    li = ldot < 0 ? left.length() : ldot + 1;
    ri = rdot < 0 ? right.length() : rdot + 1;
  }
  return 0;
}

String bytesToHex(const unsigned char *bytes, size_t length) {
  static const char *hex = "0123456789abcdef";
  String output;
  output.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    output += hex[(bytes[i] >> 4) & 0x0F];
    output += hex[bytes[i] & 0x0F];
  }
  return output;
}

bool downloadAndInstall(const String &url, const String &expectedSha256,
                        size_t expectedSize) {
  WiFiClientSecure client;
  client.setCACert(GITHUB_ROOT_CA);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    Serial.println("Unable to initialize firmware request");
    return false;
  }

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("Firmware request failed: HTTP %d\n", status);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (expectedSize > 0 && contentLength > 0 &&
      static_cast<size_t>(contentLength) != expectedSize) {
    Serial.println("Firmware size does not match manifest");
    http.end();
    return false;
  }

  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    Update.printError(Serial);
    http.end();
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t written = 0;
  while (http.connected() && (contentLength < 0 || written < static_cast<size_t>(contentLength))) {
    size_t available = stream->available();
    if (!available) {
      delay(1);
      continue;
    }
    size_t count = stream->readBytes(buffer, min(available, sizeof(buffer)));
    if (!count) continue;

    mbedtls_sha256_update(&sha, buffer, count);
    if (Update.write(buffer, count) != count) {
      Serial.println("Flash write failed");
      Update.abort();
      mbedtls_sha256_free(&sha);
      http.end();
      return false;
    }
    written += count;
  }

  unsigned char digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  http.end();

  String actualSha256 = bytesToHex(digest, sizeof(digest));
  actualSha256.toLowerCase();
  String wanted = expectedSha256;
  wanted.toLowerCase();
  if (!wanted.isEmpty() && actualSha256 != wanted) {
    Serial.printf("SHA-256 mismatch\nExpected: %s\nActual:   %s\n",
                  wanted.c_str(), actualSha256.c_str());
    Update.abort();
    return false;
  }

  if (!Update.end(true)) {
    Update.printError(Serial);
    return false;
  }

  Serial.println("OTA update installed; rebooting");
  delay(500);
  ESP.restart();
  return true;
}

void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!isValidConfiguredCertificate()) {
    Serial.println("OTA disabled: configure GITHUB_ROOT_CA in include/config.h");
    return;
  }

  WiFiClientSecure client;
  client.setCACert(GITHUB_ROOT_CA);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, manifestUrl())) return;

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("Manifest request failed: HTTP %d\n", status);
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();
  if (error) {
    Serial.printf("Manifest parse failed: %s\n", error.c_str());
    return;
  }

  JsonObject entry = doc["firmware"][DEVICE_TYPE];
  if (entry.isNull()) {
    Serial.printf("No manifest entry for %s\n", DEVICE_TYPE);
    return;
  }

  String version = entry["version"] | "";
  String target = entry["target"] | "";
  int minHardware = entry["minimum_hardware_revision"] | 1;
  String url = entry["url"] | "";
  String sha256 = entry["sha256"] | "";
  size_t size = entry["size"] | 0;

  if (target != HARDWARE_TARGET || HARDWARE_REVISION < minHardware) {
    Serial.println("Available firmware is not compatible with this hardware");
    return;
  }

  if (compareSemver(FIRMWARE_VERSION, version) >= 0) {
    Serial.printf("Firmware is current (%s)\n", FIRMWARE_VERSION);
    return;
  }

  Serial.printf("Updating %s from %s to %s\n", DEVICE_TYPE,
                FIRMWARE_VERSION, version.c_str());
  downloadAndInstall(url, sha256, size);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

  // In production, consider only reporting availability here and requiring
  // an MQTT command before calling checkForUpdate().
  checkForUpdate();
  lastCheck = millis();
}

void loop() {
  if (millis() - lastCheck >= CHECK_INTERVAL_MS) {
    checkForUpdate();
    lastCheck = millis();
  }
  delay(50);
}
