#include <Adafruit_VL53L0X.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <DFRobotDFPlayerMini.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <mbedtls/sha256.h>
#include "DistanceSensor.h"
#include "audio_sensor_portal.h"

// -----------------------------------------------------------------------------
// Hardware map (from your main PCB)
// -----------------------------------------------------------------------------
// ESP32-C3 SuperMini main PCB:
// - I2C SDA on GPIO8 (net I2C_SDA)
// - I2C SCL on GPIO9 (net I2C_SCL)
// - VL53L0X XSHUT_1 on GPIO7
// - VL53L0X XSHUT_2 on GPIO6
// - Setup/config button on GPIO9 (BOOT button)
// - Manual override / begin button on GPIO3 (BEG_BTN)
// - Crossing lights driven through 74HC595:
//   SER (GPIO1) -> DS, SRCLK (GPIO0) -> clock, RCLK (GPIO4) -> latch
constexpr int SDA_PIN = 8;      // I2C SDA
constexpr int SCL_PIN = 9;      // I2C SCL

constexpr int ENTRANCE_SENSOR_XSHUT_PIN = 7; // L0X1 XSHUT (or L0X1 only in 1-sensor mode)
constexpr int EXIT_SENSOR_XSHUT_PIN = 6;     // L0X2 XSHUT

constexpr int SR_SER_PIN = 1;     // 74HC595 SER
constexpr int SR_CLK_PIN = 0;     // 74HC595 SRCLK
constexpr int SR_LATCH_PIN = 4;   // 74HC595 RCLK

constexpr int DFPLAYER_RX_PIN = 20;
constexpr int DFPLAYER_TX_PIN = 21;

// Buttons:
// - GPIO9 enters setup/AP portal when held.
// - GPIO3 is the manual override / begin button (BEG_BTN).
constexpr int PIN_SETUP_BUTTON = 9;
constexpr int PIN_MANUAL_BUTTON = 3;
constexpr uint32_t SETUP_HOLD_MS = 3000;
constexpr uint32_t MANUAL_DEBOUNCE_MS = 50;
constexpr size_t SERIAL_SETUP_CMD_BUFFER = 128;
constexpr uint32_t SERIAL_SETUP_CMD_TIMEOUT_MS = 3000;

// -----------------------------------------------------------------------------
// Timing + behavior defaults
// -----------------------------------------------------------------------------
constexpr uint16_t DEFAULT_OCCUPIED_THRESHOLD_MM = 200;
constexpr uint16_t DEFAULT_OCCUPIED_HYSTERESIS_MM = 50;
constexpr uint16_t DEFAULT_SENSOR_SAMPLE_PERIOD_MS = 40;
constexpr uint8_t DETECT_CONFIRM_SAMPLES = 2;
constexpr uint8_t CLEAR_CONFIRM_SAMPLES = 3;
constexpr uint16_t FLASH_INTERVAL_MS = 180;
constexpr uint32_t MAX_CROSSING_MS = 12000;
constexpr uint16_t DEFAULT_CLEAR_DELAY_MS = 800;
constexpr uint8_t DEFAULT_TRACK = 1;
constexpr uint8_t DEFAULT_VOLUME = 25;
constexpr uint16_t DEFAULT_WIFI_TIMEOUT_MS = 15000;

// -----------------------------------------------------------------------------
// OTA manifest update
// -----------------------------------------------------------------------------
#ifndef DEVICE_TYPE
#define DEVICE_TYPE "audio-sensor"
#endif
#ifndef DEVICE_DISPLAY_NAME
#define DEVICE_DISPLAY_NAME "Audio Sensor"
#endif
#ifndef HARDWARE_TARGET
#define HARDWARE_TARGET "esp32-c3"
#endif
#ifndef HARDWARE_REVISION
#define HARDWARE_REVISION 2
#endif
#ifndef HARDWARE_VERSION
#define HARDWARE_VERSION "v0.2"
#endif
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.1.6"
#endif
#ifndef OTA_REPOSITORY_OWNER
#define OTA_REPOSITORY_OWNER "GeorgeHinch"
#endif
#ifndef OTA_REPOSITORY_NAME
#define OTA_REPOSITORY_NAME "GeorgeHinch_Devices"
#endif
constexpr uint32_t OTA_CHECK_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t OTA_CHECK_BOOT_DELAY_MS = 2000;
constexpr size_t OTA_MANIFEST_DOC_SIZE = 4096;

// The ESP32-C3 Arduino core embeds the standard Mozilla-derived CA bundle in
// the firmware image. Keep OTA trust anchored to that bundle rather than
// pinning a GitHub-specific root certificate that GitHub may eventually
// change.
extern "C" {
  extern const uint8_t _binary_x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
  extern const uint8_t _binary_x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
}

void configureStandardCaBundle(WiFiClientSecure& client) {
  const size_t bundleSize = static_cast<size_t>(
      _binary_x509_crt_bundle_end - _binary_x509_crt_bundle_start);
  client.setCACertBundle(_binary_x509_crt_bundle_start, bundleSize);
}

// -----------------------------------------------------------------------------
// Wi-Fi + Portal + MQTT defaults
// -----------------------------------------------------------------------------
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;
constexpr uint32_t MQTT_RETRY_MS = 5000;
constexpr const char* DEFAULT_MQTT_PREFIX = "/trains/";
constexpr const char* CONFIG_NAMESPACE = "device_cfg";
constexpr const char* CONFIG_VERSION_KEY = "cfgver";
constexpr uint8_t CONFIG_VERSION = 1;

enum LightPattern : uint8_t {
  LIGHT_PATTERN_ALTERNATING = 0,
  LIGHT_PATTERN_STROBE = 1
};

// -----------------------------------------------------------------------------
// Data models
// -----------------------------------------------------------------------------
struct CrossingConfig {
  char wifiSsid[33] = "";
  char wifiPass[65] = "";
  bool wifiHasConfig = false;
  bool mqttEnabled = false;
  char mqttHost[64] = "";
  uint16_t mqttPort = DEFAULT_MQTT_PORT;
  char mqttUser[33] = "";
  char mqttPass[33] = "";
  char mqttPrefix[24] = "/trains/";

  uint8_t sensorCount = 2;             // 1 or 2
  uint16_t sensor1ThresholdMm = DEFAULT_OCCUPIED_THRESHOLD_MM;
  uint16_t sensor2ThresholdMm = DEFAULT_OCCUPIED_THRESHOLD_MM;
  uint16_t occupiedHysteresisMm = DEFAULT_OCCUPIED_HYSTERESIS_MM;
  uint16_t sensorSamplePeriodMs = DEFAULT_SENSOR_SAMPLE_PERIOD_MS;
  uint16_t clearDelayMs = DEFAULT_CLEAR_DELAY_MS;
  uint8_t lightPattern = LIGHT_PATTERN_ALTERNATING;

  uint8_t track = DEFAULT_TRACK;       // 1..255
  uint8_t volume = DEFAULT_VOLUME;      // 0..30

  bool manualButtonEnabled = true;
  // Manual action: 0 = none, 1 = toggle alarm (lights+sound), 2 = toggle lights-only
  uint8_t manualButtonAction = 1;
  bool defaultLightOnly = false;        // Single-light behavior default for starting events
} cfg;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
DistanceSensor entranceSensor{.xshutPin = ENTRANCE_SENSOR_XSHUT_PIN, .name = "L0X1"};
DistanceSensor exitSensor{.xshutPin = EXIT_SENSOR_XSHUT_PIN, .name = "L0X2"};
bool sensor1Present = false;
bool sensor2Present = false;
uint8_t activeSensorCount = 0;

HardwareSerial& mp3Serial = Serial1;
DFRobotDFPlayerMini mp3;
bool mp3Ready = false;
bool mp3Playing = false;

bool crossingActive = false;
bool sawEntranceEvent = false;
bool sawExitEvent = false;
bool clearTimerRunning = false;
uint32_t clearTimerStartMs = 0;
uint32_t crossingStartMs = 0;
bool clearCycleLightOnly = false;
bool lastFlash = false;
uint32_t flashLastMs = 0;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Preferences prefs;

DNSServer dnsServer;
WebServer webServer(80);
bool portalActive = false;

bool wifiHasConfig = false;
bool wifiConnected = false;

char deviceId[24] = "AUDIOSEN_000000000000";
bool lastPublishedCrossingState = false;
bool lastPublishedLightOnly = false;
uint32_t lastPublishMs = 0;
uint32_t lastMqttRetryMs = 0;
uint32_t lastSensorReadMs = 0;

uint32_t setupHoldStartMs = 0;
bool setupBtnDown = false;
uint32_t lastManualReadMs = 0;
bool lastManualRaw = HIGH;
bool lastManualStable = HIGH;

String mqttTopicCmd;
String mqttTopicLightOnlyCmd;
String mqttTopicState;
String mqttTopicLightOnlyState;
String mqttTopicOtaCmd;
String mqttTopicOtaState;

uint32_t lastOtaCheckMs = 0;
bool otaBootCheckDone = false;
bool otaUpdateQueued = false;
String otaQueuedVersion;
String otaQueuedUrl;
String otaQueuedSha;
size_t otaQueuedSize = 0;
String otaLastState = "Not checked";
char serialSetupCmd[SERIAL_SETUP_CMD_BUFFER + 1];
uint8_t serialSetupCmdLen = 0;
uint32_t serialSetupLastByteMs = 0;

// The generated configuration portal is defined in audio_sensor_portal.h.

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------
void buildDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "AUDIOSEN_%02X%02X%02X%02X%02X%02X",
           (uint8_t)mac, (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
}

void buildMqttTopics() {
  String base = String(cfg.mqttPrefix);
  if (!base.endsWith("/")) base += "/";
  base += String(deviceId);

  mqttTopicCmd = base + "/crossing/cmd";
  mqttTopicLightOnlyCmd = base + "/crossing/lightOnly";
  mqttTopicState = base + "/crossing/state";
  mqttTopicLightOnlyState = base + "/crossing/lightOnly";
  mqttTopicOtaCmd = base + "/ota/cmd";
  mqttTopicOtaState = base + "/ota/state";
}

void stopMp3() {
  if (mp3Ready && mp3Playing) {
    mp3.stop();
  }
  mp3Playing = false;
}

void startMp3() {
  if (!mp3Ready || mp3Playing) return;
  mp3.volume(cfg.volume);
  mp3.loop(cfg.track);
  mp3Playing = true;
}

void handleSerialSoundCommand(const String& cmd) {
  if (cmd == "SOUND" || cmd == "SOUND ON" || cmd == "TEST_SOUND") {
    if (!mp3Ready) {
      Serial.println("Sound test unavailable: DFPlayer is not ready.");
      return;
    }
    startMp3();
    Serial.printf("Sound test looping track %u at volume %u.\n", cfg.track, cfg.volume);
    return;
  }

  if (cmd == "SOUND OFF" || cmd == "STOP SOUND" || cmd == "STOP_SOUND") {
    stopMp3();
    Serial.println("Sound test stopped.");
  }
}

uint16_t parseRangeMm(const VL53L0X_RangingMeasurementData_t& m) {
  return m.RangeMilliMeter;
}

void setCrossingLights(bool d1On, bool d2On) {
  uint8_t srValue = 0;
  if (d1On) srValue |= 0x01; // QA / SIGNAL_OUTPUT_1_1 -> D1 header
  if (d2On) srValue |= 0x02; // QB / SIGNAL_OUTPUT_1_2 -> D2 header

  digitalWrite(SR_LATCH_PIN, LOW);
  shiftOut(SR_SER_PIN, SR_CLK_PIN, LSBFIRST, srValue);
  digitalWrite(SR_LATCH_PIN, HIGH);
}

void publishMqttState() {
  if (!cfg.mqttEnabled || !mqttClient.connected()) return;
  if ((millis() - lastPublishMs) < 250) return;
  lastPublishMs = millis();

  if (crossingActive != lastPublishedCrossingState) {
    mqttClient.publish(mqttTopicState.c_str(), crossingActive ? "ON" : "OFF", true);
    lastPublishedCrossingState = crossingActive;
  }

  if (clearCycleLightOnly != lastPublishedLightOnly) {
    mqttClient.publish(mqttTopicLightOnlyState.c_str(), clearCycleLightOnly ? "ON" : "OFF", true);
    lastPublishedLightOnly = clearCycleLightOnly;
  }
}

void publishOtaState(const String& value) {
  otaLastState = value;
  if (!cfg.mqttEnabled || !mqttClient.connected()) return;
  mqttClient.publish(mqttTopicOtaState.c_str(), value.c_str(), true);
}

String manifestUrl() {
  return String("https://github.com/") + OTA_REPOSITORY_OWNER + "/" +
         OTA_REPOSITORY_NAME + "/releases/latest/download/manifest.json";
}

String normalizeSemVer(const String& version) {
  if (version.length() > 0 && (version[0] == 'v' || version[0] == 'V')) {
    return version.substring(1);
  }
  return version;
}

int compareSemver(const String& left, const String& right) {
  int li = 0;
  int ri = 0;
  String l = normalizeSemVer(left);
  String r = normalizeSemVer(right);
  for (int component = 0; component < 3; ++component) {
    int ldot = l.indexOf('.', li);
    int rdot = r.indexOf('.', ri);
    String lp = l.substring(li, ldot < 0 ? l.length() : ldot);
    String rp = r.substring(ri, rdot < 0 ? r.length() : rdot);
    int lv = lp.toInt();
    int rv = rp.toInt();
    if (lv != rv) return lv < rv ? -1 : 1;
    li = ldot < 0 ? l.length() : ldot + 1;
    ri = rdot < 0 ? r.length() : rdot + 1;
  }
  return 0;
}

String bytesToHex(const unsigned char* bytes, size_t length) {
  static const char* hex = "0123456789abcdef";
  String output;
  output.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    output += hex[(bytes[i] >> 4) & 0x0F];
    output += hex[bytes[i] & 0x0F];
  }
  return output;
}

bool downloadAndInstall(const String& url, const String& expectedSha256, size_t expectedSize) {
  WiFiClientSecure client;
  configureStandardCaBundle(client);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    Serial.println("OTA: unable to initialize firmware request");
    publishOtaState("Update failed: request initialization");
    return false;
  }

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("OTA: firmware request failed: HTTP %d\n", status);
    publishOtaState(String("Update failed: firmware HTTP ") + String(status));
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (expectedSize > 0 && contentLength > 0 && static_cast<size_t>(contentLength) != expectedSize) {
    Serial.println("OTA: firmware size mismatch");
    publishOtaState("Update failed: firmware size mismatch");
    http.end();
    return false;
  }

  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    Update.printError(Serial);
    publishOtaState("Update failed: unable to prepare storage");
    http.end();
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  WiFiClient* stream = http.getStreamPtr();
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
      Serial.println("OTA: flash write failed");
      Update.abort();
      mbedtls_sha256_free(&sha);
      publishOtaState("Update failed: flash write");
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
    Serial.printf("OTA: SHA-256 mismatch\nExpected: %s\nActual: %s\n",
                  wanted.c_str(), actualSha256.c_str());
    Update.abort();
    publishOtaState("Update failed: verification mismatch");
    return false;
  }

  if (!Update.end(true)) {
    Update.printError(Serial);
    publishOtaState("Update failed: finalization");
    return false;
  }

  Serial.println("OTA: installed and rebooting");
  publishOtaState("Updating... restarting");
  delay(500);
  ESP.restart();
  return true;
}

void cacheManifestUpdate(const String& version, const String& url, const String& sha256, size_t size) {
  otaQueuedVersion = version;
  otaQueuedUrl = url;
  otaQueuedSha = sha256;
  otaQueuedSize = size;
  otaUpdateQueued = true;
}

void checkForOtaUpdate(bool force = false) {
  publishOtaState("Checking...");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("OTA: skipped, Wi-Fi not connected");
    publishOtaState("Unable to check: not connected");
    return;
  }

  WiFiClientSecure manifestClient;
  configureStandardCaBundle(manifestClient);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(manifestClient, manifestUrl())) {
    Serial.println("OTA: could not open manifest URL");
    publishOtaState("Unable to check: request initialization");
    return;
  }

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("OTA: manifest request failed: HTTP %d\n", status);
    publishOtaState(String("Unable to check: manifest HTTP ") + String(status));
    http.end();
    return;
  }

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(OTA_MANIFEST_DOC_SIZE);
#endif
  DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();
  if (error) {
    Serial.printf("OTA: manifest parse failed: %s\n", error.c_str());
    publishOtaState("Unable to check: invalid manifest");
    return;
  }

  JsonObject entry = doc["firmware"][DEVICE_TYPE];
  if (entry.isNull()) {
    Serial.printf("OTA: no manifest entry for %s\n", DEVICE_TYPE);
    publishOtaState("Unable to check: firmware not listed");
    return;
  }

  String version = entry["version"] | "";
  String target = entry["target"] | "";
  int minHardware = entry["minimum_hardware_revision"] | 1;
  String url = entry["url"] | "";
  String sha256 = entry["sha256"] | "";
  size_t size = entry["size"] | 0;

  if (version.isEmpty() || target.isEmpty() || url.isEmpty() || sha256.isEmpty() || size == 0) {
    Serial.println("OTA: manifest entry missing required fields");
    publishOtaState("Unable to check: incomplete manifest");
    return;
  }

  if (target != HARDWARE_TARGET) {
    Serial.println("OTA: manifest target does not match");
    publishOtaState("Unable to check: incompatible target");
    return;
  }

  if (HARDWARE_REVISION < minHardware) {
    Serial.println("OTA: hardware revision is not sufficient");
    publishOtaState("Unable to check: incompatible hardware");
    return;
  }

  int cmp = compareSemver(FIRMWARE_VERSION, version);
  if (cmp >= 0) {
    Serial.printf("OTA: current firmware already at %s\n", FIRMWARE_VERSION);
    publishOtaState(String("Up to date (v") + FIRMWARE_VERSION + ")");
    otaUpdateQueued = false;
    return;
  }

  publishOtaState(String("Update available (v") + version + ")");
  if (!force && crossingActive) {
    Serial.printf("OTA: update %s available, queue until safe.\n", version.c_str());
    cacheManifestUpdate(version, url, sha256, size);
    return;
  }

  publishOtaState(String("Updating... (v") + version + ")");
  downloadAndInstall(url, sha256, size);
}

void serviceQueuedOtaInstall() {
  if (!otaUpdateQueued || crossingActive || (WiFi.status() != WL_CONNECTED)) return;
  Serial.printf("OTA: installing queued update %s\n", otaQueuedVersion.c_str());
  publishOtaState(String("Updating... (v") + otaQueuedVersion + ")");
  if (downloadAndInstall(otaQueuedUrl, otaQueuedSha, otaQueuedSize)) {
    otaUpdateQueued = false;
  } else {
    Serial.println("OTA: queued install failed; will retry on next check");
    otaUpdateQueued = false;
    publishOtaState("Update failed: queued installation");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t(topic);
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();
  msg.toUpperCase();

  auto looksOn = [&](const String& v) {
    return (v == "1" || v == "ON" || v == "TRUE");
  };
  auto looksOff = [&](const String& v) {
    return (v == "0" || v == "OFF" || v == "FALSE");
  };

  if (t == mqttTopicCmd) {
    if (msg == "ON") {
      Serial.println("[MQTT] cmd ON -> start full alarm");
      startCrossing(false);
      return;
    }
    if (msg == "OFF") {
      Serial.println("[MQTT] cmd OFF -> stop alarm");
      if (crossingActive) stopMp3();
      crossingActive = false;
      clearCycleLightOnly = false;
      clearTimerRunning = false;
      sawEntranceEvent = false;
      sawExitEvent = false;
      publishMqttState();
      return;
    } else if (msg == "LIGHTS_ONLY_ON") {
      clearCycleLightOnly = true;
      startCrossing(true);
      publishMqttState();
      return;
    } else if (msg == "LIGHTS_ONLY_OFF") {
      clearCycleLightOnly = false;
      if (crossingActive) {
        startMp3(); // ensure sound restores if possible
      }
      publishMqttState();
      return;
    }
    if (msg == "TOGGLE") {
      if (crossingActive) {
        if (clearCycleLightOnly) {
          clearCycleLightOnly = false;
        } else {
          clearCycleLightOnly = true;
        }
        crossingActive = true;
        if (clearCycleLightOnly) stopMp3();
        else startMp3();
      } else {
        startCrossing(cfg.defaultLightOnly);
      }
    }
    return;
  }

  if (t == mqttTopicLightOnlyCmd) {
    if (looksOn(msg)) {
      clearCycleLightOnly = true;
    } else if (looksOff(msg)) {
      clearCycleLightOnly = false;
    } else if (msg == "TOGGLE") {
      clearCycleLightOnly = !clearCycleLightOnly;
    }

    if (crossingActive) {
      if (clearCycleLightOnly) {
        stopMp3();
      } else {
        startMp3();
      }
    }
    publishMqttState();
    return;
  }

  if (t == mqttTopicOtaCmd) {
    if (msg == "CHECK") {
      Serial.println("[MQTT] ota CHECK requested");
      checkForOtaUpdate(false);
      return;
    }
    if (msg == "FORCE") {
      Serial.println("[MQTT] ota FORCE requested");
      checkForOtaUpdate(true);
      return;
    }
    if (msg == "STATUS") {
      if (!cfg.mqttEnabled || !mqttClient.connected()) return;
      mqttClient.publish(mqttTopicOtaState.c_str(), otaLastState.c_str(), true);
      return;
    }
  }
}

// -----------------------------------------------------------------------------
// NVS config
// -----------------------------------------------------------------------------
void saveConfig() {
  prefs.begin(CONFIG_NAMESPACE, false);
  prefs.putUInt(CONFIG_VERSION_KEY, CONFIG_VERSION);
  prefs.putString("wifiSsid", cfg.wifiSsid);
  prefs.putString("wifiPass", cfg.wifiPass);
  prefs.putBool("mqttEn", cfg.mqttEnabled);
  prefs.putString("mqttHost", cfg.mqttHost);
  prefs.putUShort("mqttPort", cfg.mqttPort);
  prefs.putString("mqttUser", cfg.mqttUser);
  prefs.putString("mqttPass", cfg.mqttPass);
  prefs.putString("mqttPref", cfg.mqttPrefix);
  prefs.putUChar("sens", cfg.sensorCount);
  prefs.putUShort("thr1", cfg.sensor1ThresholdMm);
  prefs.putUShort("thr2", cfg.sensor2ThresholdMm);
  prefs.putUShort("hyst", cfg.occupiedHysteresisMm);
  prefs.putUShort("sample", cfg.sensorSamplePeriodMs);
  prefs.putUInt("clrDelay", cfg.clearDelayMs);
  prefs.putUChar("lightPat", cfg.lightPattern);
  prefs.putUChar("track", cfg.track);
  prefs.putUChar("vol", cfg.volume);
  prefs.putBool("manEn", cfg.manualButtonEnabled);
  prefs.putUChar("manAct", cfg.manualButtonAction);
  prefs.putBool("lightOnly", cfg.defaultLightOnly);
  prefs.end();
}

void loadConfig() {
  prefs.begin(CONFIG_NAMESPACE, true);
  cfg.sensorCount = constrain((uint8_t)prefs.getUChar("sens", cfg.sensorCount), 1, 2);
  cfg.sensor1ThresholdMm = constrain((uint16_t)prefs.getUShort("thr1", cfg.sensor1ThresholdMm), 20, 2000);
  cfg.sensor2ThresholdMm = constrain((uint16_t)prefs.getUShort("thr2", cfg.sensor2ThresholdMm), 20, 2000);
  cfg.occupiedHysteresisMm = constrain((uint16_t)prefs.getUShort("hyst", cfg.occupiedHysteresisMm), 0, 500);
  cfg.sensorSamplePeriodMs = constrain((uint16_t)prefs.getUShort("sample", cfg.sensorSamplePeriodMs), 30, 1000);
  cfg.clearDelayMs = constrain((uint16_t)prefs.getUInt("clrDelay", cfg.clearDelayMs), 0, 20000);
  cfg.lightPattern = (uint8_t)constrain((int)prefs.getUChar("lightPat", cfg.lightPattern), 0, 1);
  cfg.track = (uint8_t)constrain((int)prefs.getUChar("track", cfg.track), 1, 255);
  cfg.volume = (uint8_t)constrain((int)prefs.getUChar("vol", cfg.volume), 0, 30);
  cfg.mqttEnabled = prefs.getBool("mqttEn", cfg.mqttEnabled);
  cfg.manualButtonEnabled = prefs.getBool("manEn", cfg.manualButtonEnabled);
  cfg.manualButtonAction = (uint8_t)constrain((int)prefs.getUChar("manAct", cfg.manualButtonAction), 0, 2);
  cfg.defaultLightOnly = prefs.getBool("lightOnly", cfg.defaultLightOnly);
  cfg.wifiHasConfig = false;
  prefs.getString("wifiSsid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  if (strlen(cfg.wifiSsid) > 0) cfg.wifiHasConfig = true;
  prefs.getString("wifiPass", cfg.wifiPass, sizeof(cfg.wifiPass));
  prefs.getString("mqttHost", cfg.mqttHost, sizeof(cfg.mqttHost));
  prefs.getString("mqttUser", cfg.mqttUser, sizeof(cfg.mqttUser));
  prefs.getString("mqttPass", cfg.mqttPass, sizeof(cfg.mqttPass));
  prefs.getString("mqttPref", cfg.mqttPrefix, sizeof(cfg.mqttPrefix));
  if (strlen(cfg.mqttPrefix) == 0) {
    strlcpy(cfg.mqttPrefix, DEFAULT_MQTT_PREFIX, sizeof(cfg.mqttPrefix));
  }
  cfg.mqttPort = (uint16_t)constrain((int)prefs.getUInt("mqttPort", cfg.mqttPort), 1, 65535);
  prefs.end();
}

void readConfigFormValue(const String& key, char* out, size_t maxLen) {
  if (!webServer.hasArg(key)) return;
  String v = webServer.arg(key);
  v.trim();
  v.toCharArray(out, (int)maxLen);
}

void handlePortalRoot() {
  webServer.sendHeader("Content-Encoding", "gzip");
  webServer.send_P(200, "text/html; charset=utf-8",
                   reinterpret_cast<PGM_P>(AUDIO_SENSOR_PORTAL_HTML),
                   AUDIO_SENSOR_PORTAL_HTML_LENGTH);
}

void handlePortalConfig() {
  JsonDocument doc;
  doc["deviceId"] = deviceId;
  doc["ip"] = portalActive ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["firmware"] = FIRMWARE_VERSION;
  doc["configSsid"] = deviceId;
  doc["wifiSsid"] = cfg.wifiSsid;
  doc["mqttEnabled"] = cfg.mqttEnabled;
  doc["mqttHost"] = cfg.mqttHost;
  doc["mqttPort"] = cfg.mqttPort;
  doc["mqttUser"] = cfg.mqttUser;
  doc["mqttPrefix"] = cfg.mqttPrefix;
  doc["sensorCount"] = cfg.sensorCount;
  JsonArray sensors = doc["sensors"].to<JsonArray>();
  JsonObject sensor1 = sensors.add<JsonObject>();
  sensor1["present"] = sensor1Present;
  sensor1["threshold"] = cfg.sensor1ThresholdMm;
  JsonObject sensor2 = sensors.add<JsonObject>();
  sensor2["present"] = sensor2Present;
  sensor2["threshold"] = cfg.sensor2ThresholdMm;
  doc["hysteresis"] = cfg.occupiedHysteresisMm;
  doc["samplePeriod"] = cfg.sensorSamplePeriodMs;
  doc["clearDelayMs"] = cfg.clearDelayMs;
  doc["lightPattern"] = cfg.lightPattern;
  doc["defaultLightOnly"] = cfg.defaultLightOnly;
  doc["playerPresent"] = mp3Ready;
  doc["track"] = cfg.track;
  doc["volume"] = cfg.volume;
  doc["manualButtonEnabled"] = cfg.manualButtonEnabled;
  doc["manualButtonAction"] = cfg.manualButtonAction;
  doc["otaState"] = otaLastState;
  String json;
  serializeJson(doc, json);
  webServer.send(200, "application/json; charset=utf-8", json);
}

void handlePortalSave() {
  readConfigFormValue("wifiSsid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  if (webServer.hasArg("wifiPass") && webServer.arg("wifiPass").length() > 0) {
    readConfigFormValue("wifiPass", cfg.wifiPass, sizeof(cfg.wifiPass));
  }
  readConfigFormValue("mqttHost", cfg.mqttHost, sizeof(cfg.mqttHost));
  readConfigFormValue("mqttUser", cfg.mqttUser, sizeof(cfg.mqttUser));
  if (webServer.hasArg("mqttPass") && webServer.arg("mqttPass").length() > 0) {
    readConfigFormValue("mqttPass", cfg.mqttPass, sizeof(cfg.mqttPass));
  }
  readConfigFormValue("mqttPrefix", cfg.mqttPrefix, sizeof(cfg.mqttPrefix));

  cfg.mqttEnabled = webServer.hasArg("mqttEnabled");
  if (webServer.hasArg("mqttPort")) {
    cfg.mqttPort = constrain((uint16_t)webServer.arg("mqttPort").toInt(), 1, 65535);
  }
  if (webServer.hasArg("sensorCount")) {
    cfg.sensorCount = (uint8_t)constrain((int)webServer.arg("sensorCount").toInt(), 1, 2);
  }
  if (webServer.hasArg("thr1")) {
    cfg.sensor1ThresholdMm = constrain((uint16_t)webServer.arg("thr1").toInt(), 20, 2000);
  }
  if (webServer.hasArg("thr2")) {
    cfg.sensor2ThresholdMm = constrain((uint16_t)webServer.arg("thr2").toInt(), 20, 2000);
  }
  if (webServer.hasArg("hyst")) {
    cfg.occupiedHysteresisMm = constrain((uint16_t)webServer.arg("hyst").toInt(), 0, 500);
  }
  if (webServer.hasArg("sample")) {
    cfg.sensorSamplePeriodMs = constrain((uint16_t)webServer.arg("sample").toInt(), 30, 1000);
  }
  if (webServer.hasArg("clearDelayMs")) {
    cfg.clearDelayMs = constrain((uint16_t)webServer.arg("clearDelayMs").toInt(), 0, 20000);
  }
  if (webServer.hasArg("lightPattern")) {
    cfg.lightPattern = (uint8_t)constrain((int)webServer.arg("lightPattern").toInt(), 0, 1);
  }
  if (webServer.hasArg("track")) {
    cfg.track = (uint8_t)constrain(webServer.arg("track").toInt(), 1, 255);
  }
  if (webServer.hasArg("volume")) {
    cfg.volume = (uint8_t)constrain(webServer.arg("volume").toInt(), 0, 30);
  }
  cfg.defaultLightOnly = webServer.hasArg("defaultLightOnly");
  cfg.manualButtonEnabled = webServer.hasArg("manualButtonEnabled");
  if (webServer.hasArg("manualButtonAction")) {
    cfg.manualButtonAction = (uint8_t)constrain((int)webServer.arg("manualButtonAction").toInt(), 0, 2);
  }

  if (strlen(cfg.mqttPrefix) == 0) {
    strlcpy(cfg.mqttPrefix, DEFAULT_MQTT_PREFIX, sizeof(cfg.mqttPrefix));
  }

  if (cfg.mqttPrefix[strlen(cfg.mqttPrefix) - 1] != '/') {
    if (strlen(cfg.mqttPrefix) < sizeof(cfg.mqttPrefix) - 1) {
      strlcat(cfg.mqttPrefix, "/", sizeof(cfg.mqttPrefix));
    }
  }

  saveConfig();
  webServer.send(200, "text/html", "<!doctype html><body><h3>Saved. Restarting...</h3></body>");
  delay(900);
  ESP.restart();
}

void handlePortalReset() {
  const String action = webServer.arg("action");
  if (action == "wifi") {
    cfg.wifiSsid[0] = '\0';
    cfg.wifiPass[0] = '\0';
    cfg.wifiHasConfig = false;
    saveConfig();
  } else if (action == "mqtt") {
    cfg.mqttEnabled = false;
    cfg.mqttHost[0] = '\0';
    cfg.mqttPort = DEFAULT_MQTT_PORT;
    cfg.mqttUser[0] = '\0';
    cfg.mqttPass[0] = '\0';
    strlcpy(cfg.mqttPrefix, DEFAULT_MQTT_PREFIX, sizeof(cfg.mqttPrefix));
    saveConfig();
  } else if (action == "factory") {
    prefs.begin(CONFIG_NAMESPACE, false);
    prefs.clear();
    prefs.end();
  } else {
    webServer.send(400, "text/plain; charset=utf-8", "Unknown reset action");
    return;
  }

  webServer.send(200, "text/plain; charset=utf-8", "Reset accepted; restarting");
  delay(500);
  ESP.restart();
}

void handlePortalOta() {
  String action = webServer.hasArg("action") ? webServer.arg("action") : "STATUS";
  action.trim();
  action.toUpperCase();

  if (action == "CHECK") {
    Serial.println("[WEB] OTA CHECK requested");
    checkForOtaUpdate(false);
  } else if (action == "FORCE") {
    Serial.println("[WEB] OTA FORCE requested");
    checkForOtaUpdate(true);
  } else if (action == "STATUS") {
    // no-op; page refresh
  } else {
    otaLastState = String("Unknown OTA action: ") + action;
  }

  webServer.sendHeader("Location", "/", true);
  webServer.send(303, "text/plain", "");
}

void startPortal() {
  portalActive = true;
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  String apName = String(deviceId);
  WiFi.softAP(apName.c_str(), "");  // open AP for setup

  dnsServer.start(53, "*", WiFi.softAPIP());
  webServer.on("/", HTTP_GET, handlePortalRoot);
  webServer.on("/api/config", HTTP_GET, handlePortalConfig);
  webServer.on("/save", HTTP_POST, handlePortalSave);
  webServer.on("/reset", HTTP_POST, handlePortalReset);
  webServer.on("/ota", HTTP_ANY, handlePortalOta);
  webServer.onNotFound([]() {
    webServer.send(302, "text/plain", "");
    webServer.sendHeader("Location", "/", true);
  });
  webServer.begin();

  Serial.printf("Setup AP started: %s (%s)\n", apName.c_str(), WiFi.softAPIP().toString().c_str());
}

bool getSerialCommandArgument(const String& rawCmd, const String& upperCmd,
                              const char* prefix, String& value) {
  const String prefixString = String(prefix);
  if (!upperCmd.startsWith(prefixString)) return false;
  if (upperCmd.length() <= prefixString.length() ||
      upperCmd.charAt(prefixString.length()) != ' ') {
    return false;
  }

  value = rawCmd.substring(prefixString.length() + 1);
  value.trim();
  return true;
}

void printSerialWifiHelp() {
  Serial.println("Wi-Fi serial setup commands:");
  Serial.println("  WIFI SSID <network name>");
  Serial.println("  WIFI PASSWORD <password>");
  Serial.println("  WIFI STATUS");
  Serial.println("  WIFI SAVE   (save credentials and restart)");
  Serial.println("  WIFI CLEAR  (erase credentials and restart)");
  Serial.println("The shorter SSID and PASSWORD forms are also accepted.");
}

bool handleSerialWifiCommand(const String& rawCmd, const String& upperCmd) {
  if (upperCmd == "WIFI" || upperCmd == "WIFI HELP" || upperCmd == "HELP WIFI") {
    printSerialWifiHelp();
    return true;
  }

  if (upperCmd == "WIFI STATUS") {
    Serial.printf("Wi-Fi SSID: %s\n", strlen(cfg.wifiSsid) > 0 ? cfg.wifiSsid : "(not set)");
    Serial.printf("Wi-Fi password: %s\n", strlen(cfg.wifiPass) > 0 ? "set" : "(not set)");
    Serial.printf("Wi-Fi saved: %s\n", cfg.wifiHasConfig ? "yes" : "no");
    return true;
  }

  if (upperCmd == "WIFI CLEAR") {
    cfg.wifiSsid[0] = '\0';
    cfg.wifiPass[0] = '\0';
    cfg.wifiHasConfig = false;
    saveConfig();
    Serial.println("Wi-Fi credentials cleared. Restarting into setup portal...");
    delay(500);
    ESP.restart();
    return true;
  }

  if (upperCmd == "WIFI SAVE") {
    if (strlen(cfg.wifiSsid) == 0) {
      Serial.println("Wi-Fi save failed: enter an SSID first.");
      return true;
    }

    cfg.wifiHasConfig = true;
    saveConfig();
    Serial.println("Wi-Fi credentials saved. Restarting...");
    delay(500);
    ESP.restart();
    return true;
  }

  String value;
  if (getSerialCommandArgument(rawCmd, upperCmd, "WIFI SSID", value) ||
      getSerialCommandArgument(rawCmd, upperCmd, "SSID", value)) {
    if (value.length() == 0) {
      Serial.println("SSID cannot be empty.");
      return true;
    }
    strlcpy(cfg.wifiSsid, value.c_str(), sizeof(cfg.wifiSsid));
    cfg.wifiHasConfig = true;
    Serial.printf("Wi-Fi SSID set to '%s'. Enter WIFI PASSWORD <value>, then WIFI SAVE.\n",
                  cfg.wifiSsid);
    return true;
  }

  if (getSerialCommandArgument(rawCmd, upperCmd, "WIFI PASSWORD", value) ||
      getSerialCommandArgument(rawCmd, upperCmd, "WIFI PASS", value) ||
      getSerialCommandArgument(rawCmd, upperCmd, "PASSWORD", value) ||
      getSerialCommandArgument(rawCmd, upperCmd, "PASS", value)) {
    strlcpy(cfg.wifiPass, value.c_str(), sizeof(cfg.wifiPass));
    Serial.println("Wi-Fi password set. Enter WIFI SAVE to store it and restart.");
    return true;
  }

  return false;
}

void serviceSetupPortal() {
  if (!portalActive) return;
  dnsServer.processNextRequest();
  webServer.handleClient();
  checkSerialForSetupCommand();
  delay(2);
}

void enterSetupPortal(const char* reason) {
  if (portalActive) return;
  if (cfg.mqttEnabled && mqttClient.connected()) {
    mqttClient.disconnect();
  }
  Serial.printf("Starting setup portal: %s\n", reason);
  startPortal();
}

void checkSerialForSetupCommand() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      serialSetupCmd[serialSetupCmdLen] = '\0';
      String rawCmd = String(serialSetupCmd);
      rawCmd.trim();
      String cmd = rawCmd;
      cmd.toUpperCase();

      if (cmd == "SOUND" || cmd == "SOUND ON" || cmd == "TEST_SOUND" ||
          cmd == "SOUND OFF" || cmd == "STOP SOUND" || cmd == "STOP_SOUND") {
        handleSerialSoundCommand(cmd);
      } else if (handleSerialWifiCommand(rawCmd, cmd)) {
        // Wi-Fi setup command handled above.
      } else if (cmd == "AP" || cmd == "PORTAL" || cmd == "SETUP") {
        if (portalActive) {
          Serial.println("Setup portal is already active.");
        } else {
          Serial.println("Serial AP command received.");
          serialSetupCmdLen = 0;
          serialSetupLastByteMs = millis();
          enterSetupPortal("serial");
        }
      } else if (cmd.length() > 0) {
        Serial.print("Unknown serial command: ");
        Serial.println(cmd);
      }

      serialSetupCmdLen = 0;
      continue;
    }

    if (serialSetupCmdLen == 0) {
      serialSetupLastByteMs = millis();
    } else if ((millis() - serialSetupLastByteMs) > SERIAL_SETUP_CMD_TIMEOUT_MS) {
      serialSetupCmdLen = 0;
    }

    if (serialSetupCmdLen < SERIAL_SETUP_CMD_BUFFER) {
      serialSetupCmd[serialSetupCmdLen++] = c;
    }

    serialSetupLastByteMs = millis();
  }

  if (serialSetupCmdLen > 0 && (millis() - serialSetupLastByteMs) > SERIAL_SETUP_CMD_TIMEOUT_MS) {
    serialSetupCmdLen = 0;
    Serial.println("Serial command input timed out. Type AP + Enter.");
  }
}

// -----------------------------------------------------------------------------
// Sensor logic
// -----------------------------------------------------------------------------
void updateSensorState(DistanceSensor& sensor, uint16_t occupiedThresholdMm) {
  sensor.triggeredThisCycle = false;
  sensor.previouslyOccupied = sensor.occupied;

  VL53L0X_RangingMeasurementData_t measurement;
  sensor.vl53.rangingTest(&measurement, false);

  if (measurement.RangeStatus == 0) {
    sensor.readingValid = true;
    sensor.distanceMm = parseRangeMm(measurement);

    if (sensor.distanceMm <= occupiedThresholdMm) {
      sensor.clearSamples = 0;
      if (!sensor.occupied && sensor.detectSamples < DETECT_CONFIRM_SAMPLES) {
        sensor.detectSamples++;
      }
      if (sensor.detectSamples >= DETECT_CONFIRM_SAMPLES) {
        sensor.occupied = true;
      }
    } else if (sensor.distanceMm >=
               static_cast<uint32_t>(occupiedThresholdMm) + cfg.occupiedHysteresisMm) {
      sensor.detectSamples = 0;
      if (sensor.occupied && sensor.clearSamples < CLEAR_CONFIRM_SAMPLES) {
        sensor.clearSamples++;
      }
      if (sensor.clearSamples >= CLEAR_CONFIRM_SAMPLES) {
        sensor.occupied = false;
      }
    } else {
      sensor.detectSamples = 0;
      sensor.clearSamples = 0;
    }
  } else {
    // Treat intermittent misses as potentially clear, after debounce.
    sensor.readingValid = false;
    sensor.detectSamples = 0;
    if (sensor.occupied && sensor.clearSamples < CLEAR_CONFIRM_SAMPLES) {
      sensor.clearSamples++;
    }
    if (sensor.clearSamples >= CLEAR_CONFIRM_SAMPLES) {
      sensor.occupied = false;
    }
  }

  sensor.triggeredThisCycle = (!sensor.previouslyOccupied && sensor.occupied);
}

void updateAllSensors() {
  if (!sensor1Present && !sensor2Present) return;

  if (sensor1Present) updateSensorState(entranceSensor, cfg.sensor1ThresholdMm);
  if (sensor2Present) updateSensorState(exitSensor, cfg.sensor2ThresholdMm);

  if (sensor1Present && entranceSensor.triggeredThisCycle) {
    sawEntranceEvent = true;
  }
  if (sensor2Present && exitSensor.triggeredThisCycle) {
    sawExitEvent = true;
  }
}

bool initL0xSensor(DistanceSensor& sensor, uint8_t i2cAddress) {
  pinMode(sensor.xshutPin, OUTPUT);
  digitalWrite(sensor.xshutPin, LOW);
  delay(5);
  digitalWrite(sensor.xshutPin, HIGH);
  delay(10);

  if (!sensor.vl53.begin(0x29, false, &Wire)) {
    Serial.print("Could not initialize ");
    Serial.println(sensor.name);
    return false;
  }

  sensor.vl53.setAddress(i2cAddress);
  Serial.print(sensor.name);
  Serial.print(" initialized at 0x");
  Serial.println(i2cAddress, HEX);
  return true;
}

void updateFlashingLights() {
  if (!crossingActive) {
    setCrossingLights(false, false);
    return;
  }

  const uint32_t now = millis();
  if (now - flashLastMs >= FLASH_INTERVAL_MS) {
    flashLastMs = now;
    lastFlash = !lastFlash;
  }
  if (cfg.lightPattern == LIGHT_PATTERN_STROBE) {
    setCrossingLights(lastFlash, lastFlash);
  } else {
    setCrossingLights(lastFlash, !lastFlash);
  }
}

void stopCrossing() {
  if (!crossingActive) return;
  crossingActive = false;
  clearTimerRunning = false;
  sawEntranceEvent = false;
  sawExitEvent = false;
  clearCycleLightOnly = false;

  stopMp3();
  setCrossingLights(false, false);
  Serial.println("Crossing stop");
  publishMqttState();
}

void startCrossing(bool lightOnlyRequest) {
  if (crossingActive) {
    // Update only the request mode.
    clearCycleLightOnly = lightOnlyRequest;
    clearCycleLightOnly ? stopMp3() : startMp3();
    publishMqttState();
    return;
  }
  crossingActive = true;
  flashLastMs = millis();
  lastFlash = false;
  crossingStartMs = millis();
  clearTimerRunning = false;
  sawEntranceEvent = false;
  sawExitEvent = false;
  clearCycleLightOnly = lightOnlyRequest;
  if (clearCycleLightOnly) {
    stopMp3();
  } else {
    startMp3();
  }
  Serial.println("Crossing start");
  publishMqttState();
}

void evaluateCrossingLogic() {
  if (!crossingActive) {
    if (cfg.sensorCount == 1) {
      const bool singleSensorTriggered =
          (sensor1Present && entranceSensor.occupied) || (sensor2Present && exitSensor.occupied);
      if (singleSensorTriggered) {
        startCrossing(cfg.defaultLightOnly);
        if (sensor1Present && entranceSensor.occupied) sawEntranceEvent = true;
        if (sensor2Present && exitSensor.occupied) sawExitEvent = true;
      }
    } else {
      const bool anyOccupied = (sensor1Present && entranceSensor.occupied) || (sensor2Present && exitSensor.occupied);
      if (anyOccupied) {
        startCrossing(cfg.defaultLightOnly);
        // remember event sides for clear logic
        if (sensor1Present && entranceSensor.occupied) sawEntranceEvent = true;
        if (sensor2Present && exitSensor.occupied) sawExitEvent = true;
      }
    }
    return;
  }

  bool clearCandidate = false;
  if (cfg.sensorCount == 1) {
    if (sensor1Present) {
      clearCandidate = !entranceSensor.occupied;
    } else if (sensor2Present) {
      clearCandidate = !exitSensor.occupied;
    } else {
      clearCandidate = false;
    }
  } else {
    const bool bothClear = (!entranceSensor.occupied && !exitSensor.occupied);
    clearCandidate = bothClear && sawEntranceEvent && sawExitEvent;
  }

  if (clearCandidate) {
    if (!clearTimerRunning) {
      clearTimerRunning = true;
      clearTimerStartMs = millis();
    } else if (millis() - clearTimerStartMs >= cfg.clearDelayMs) {
      stopCrossing();
      return;
    }
  } else {
    clearTimerRunning = false;
  }

  if (cfg.clearDelayMs == 0 && clearCandidate) {
    stopCrossing();
    return;
  }

  if (millis() - crossingStartMs >= MAX_CROSSING_MS) {
    Serial.println("Crossing timeout; forcing clear.");
    stopCrossing();
    return;
  }

  // Keep mp3 synced with light-only state.
  if (clearCycleLightOnly) {
    stopMp3();
  } else {
    startMp3();
  }
}

void checkManualButton() {
  if (PIN_MANUAL_BUTTON == SR_CLK_PIN || PIN_MANUAL_BUTTON == SR_SER_PIN || PIN_MANUAL_BUTTON == SR_LATCH_PIN) {
    return; // conflict with required shift-register control pins on this PCB
  }
  if (!cfg.manualButtonEnabled) return;
  const bool reading = (digitalRead(PIN_MANUAL_BUTTON) == LOW);
  const uint32_t now = millis();

  if (reading != lastManualRaw) {
    lastManualReadMs = now;
    lastManualRaw = reading;
  }

  if ((now - lastManualReadMs) > MANUAL_DEBOUNCE_MS && reading != lastManualStable) {
    lastManualStable = reading;
    if (!lastManualStable) {
      switch (cfg.manualButtonAction) {
        case 0:
          break;
        case 1: {
          if (crossingActive) {
            stopCrossing();
          } else {
            startCrossing(false);
          }
          break;
        }
        case 2: {
          if (crossingActive) {
            stopCrossing();
          } else {
            startCrossing(true);
          }
          break;
        }
      }
    }
  }
}

bool checkSetupButton() {
  const bool pressed = digitalRead(PIN_SETUP_BUTTON) == LOW;
  const uint32_t now = millis();

  if (!pressed) {
    setupBtnDown = false;
    return false;
  }

  if (!setupBtnDown) {
    setupBtnDown = true;
    setupHoldStartMs = now;
  } else if (now - setupHoldStartMs >= SETUP_HOLD_MS) {
    setupBtnDown = false;
    Serial.println("BOOT held for 3 seconds: entering setup portal");
    enterSetupPortal("BOOT button hold");
  }
  return true;
}

void setupMqtt() {
  if (!cfg.mqttEnabled || strlen(cfg.mqttHost) == 0) {
    return;
  }
  mqttClient.setServer(cfg.mqttHost, cfg.mqttPort);
  mqttClient.setCallback(mqttCallback);
  buildMqttTopics();
}

void connectMqttIfNeeded() {
  if (!cfg.mqttEnabled || !WiFi.isConnected()) return;
  if (mqttClient.connected()) return;
  if ((millis() - lastMqttRetryMs) < MQTT_RETRY_MS) return;
  lastMqttRetryMs = millis();

  String clientId = String(deviceId);
  bool ok = false;
  if (strlen(cfg.mqttUser) > 0) {
    ok = mqttClient.connect(clientId.c_str(), cfg.mqttUser, cfg.mqttPass);
  } else {
    ok = mqttClient.connect(clientId.c_str());
  }

  if (!ok) {
    Serial.printf("MQTT connect failed, rc=%d\n", mqttClient.state());
    return;
  }

  Serial.println("MQTT connected");
  mqttClient.subscribe(mqttTopicCmd.c_str());
  mqttClient.subscribe(mqttTopicLightOnlyCmd.c_str());
  mqttClient.subscribe(mqttTopicOtaCmd.c_str());
  publishMqttState();
  publishOtaState("CONNECTED");
}

void connectWiFiOrLocal() {
  if (!cfg.wifiHasConfig || strlen(cfg.wifiSsid) == 0) {
    startPortal();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

  const uint32_t start = millis();
  Serial.printf("Connecting to Wi-Fi '%s'...", cfg.wifiSsid);
  while (millis() - start < DEFAULT_WIFI_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.print(" connected: ");
      Serial.println(WiFi.localIP());
      return;
    }
    delay(250);
  }

  Serial.println("\nWi-Fi timeout: continuing local mode.");
  wifiConnected = false;
}

// -----------------------------------------------------------------------------
// Main setup / loop
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("Audio Sensor ready for serial commands.");
  Serial.println("Type AP, PORTAL, or SETUP and press Enter to open setup AP mode.");

  // Setup output pins
  pinMode(SR_SER_PIN, OUTPUT);
  pinMode(SR_CLK_PIN, OUTPUT);
  pinMode(SR_LATCH_PIN, OUTPUT);
  setCrossingLights(false, false);

  cfg.wifiHasConfig = false;
  loadConfig();
  buildDeviceId();
  buildMqttTopics();

  Serial.printf("Device ID: %s\n", deviceId);

  pinMode(PIN_SETUP_BUTTON, INPUT_PULLUP);

  if (PIN_MANUAL_BUTTON != SR_SER_PIN && PIN_MANUAL_BUTTON != SR_CLK_PIN && PIN_MANUAL_BUTTON != SR_LATCH_PIN) {
    pinMode(PIN_MANUAL_BUTTON, INPUT_PULLUP);
  } else if (cfg.manualButtonEnabled) {
    Serial.println("Manual button disabled: configured manual pin conflicts with shift-register control pins.");
    cfg.manualButtonEnabled = false;
  }
  // Fallback pull-ups for I2C bus in case external pull-ups are absent on mounted breakouts.
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);

  // I2C + sensors
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  pinMode(entranceSensor.xshutPin, OUTPUT);
  pinMode(exitSensor.xshutPin, OUTPUT);
  digitalWrite(entranceSensor.xshutPin, LOW);
  digitalWrite(exitSensor.xshutPin, LOW);
  delay(10);

  sensor1Present = initL0xSensor(entranceSensor, 0x2A);
  if (cfg.sensorCount >= 2) {
    sensor2Present = initL0xSensor(exitSensor, 0x2B);
  } else {
    sensor2Present = false;
  }
  activeSensorCount = (uint8_t)(sensor1Present ? 1 : 0) + (uint8_t)(sensor2Present ? 1 : 0);
  if (activeSensorCount == 0) {
    Serial.println("No VL53L0X sensors initialized. System will stay in local mode and flash on manual commands only.");
  } else if (activeSensorCount < cfg.sensorCount) {
    cfg.sensorCount = activeSensorCount;
    Serial.printf("Only %u sensor detected, switching sensorCount to %u\n", activeSensorCount, cfg.sensorCount);
  }

  // DFPlayer
  mp3Serial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  delay(80);
  if (mp3.begin(mp3Serial, true, false)) {
    mp3Ready = true;
    mp3.volume(cfg.volume);
    Serial.println("DFPlayer ready");
  } else {
    mp3Ready = false;
    Serial.println("DFPlayer not detected (continuing local lights-only mode)");
  }

  connectWiFiOrLocal();

  if (WiFi.status() == WL_CONNECTED) {
    setupMqtt();
    connectMqttIfNeeded();
    lastOtaCheckMs = millis();
    Serial.printf("Firmware version: %s\n", FIRMWARE_VERSION);
  }

  Serial.println("Controller ready.");
}

void loop() {
  if (portalActive) {
    serviceSetupPortal();
    return;
  }

  checkSerialForSetupCommand();
  const bool setupButtonHeld = checkSetupButton();
  if (portalActive) return;

  if (WiFi.status() == WL_CONNECTED && cfg.mqttEnabled) {
    connectMqttIfNeeded();
    mqttClient.loop();
  }

  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED && (now >= OTA_CHECK_BOOT_DELAY_MS) && !otaBootCheckDone) {
    otaBootCheckDone = true;
    checkForOtaUpdate(false);
    lastOtaCheckMs = now;
  } else if (WiFi.status() == WL_CONNECTED && (now - lastOtaCheckMs) >= OTA_CHECK_INTERVAL_MS) {
    checkForOtaUpdate(false);
    lastOtaCheckMs = now;
  }
  serviceQueuedOtaInstall();

  // GPIO9 is shared by BOOT and I2C SCL. Pausing sensor traffic while BOOT is
  // held lets the three-second gesture complete without fighting the bus.
  if (!setupButtonHeld && (now - lastSensorReadMs) >= cfg.sensorSamplePeriodMs) {
    lastSensorReadMs = now;
    updateAllSensors();
    evaluateCrossingLogic();
  }

  checkManualButton();
  updateFlashingLights();
  publishMqttState();

  delay(5);
}
