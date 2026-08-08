/*
 * Triple DFPlayer JMRI MQTT Controller — ESP32-C3 Super Mini
 * ============================================================
 *
 * Controls three DFPlayer Mini modules independently via MQTT,
 * with full JMRI auto-discovery and name-sync integration.
 *
 * Each DFPlayer (P1, P2, P3) supports three play modes:
 *   LOOP_TRACK  — continuously loop a single track
 *   SINGLE      — play one track once when triggered
 *   LOOP_FOLDER — loop all tracks in a folder
 *
 * JMRI objects per player (×3):
 *   Turnout  Pn_MUTE    — THROWN = muted, CLOSED = unmuted
 *   Turnout  Pn_PLAY    — THROWN = start playback, CLOSED = stop
 *   Sensor   Pn_PLAYING — ACTIVE = playing, INACTIVE = stopped
 *   Memory   Pn_VOL     — volume level (0–30)
 *   Memory   Pn_MODE    — LOOP_TRACK | SINGLE | LOOP_FOLDER
 *   Memory   Pn_TRACK   — track number (1–255)
 *   Memory   Pn_FOLDER  — folder number (1–99)
 *
 * Hardware (2 pins per DFPlayer — TX/RX only, no BUSY pin):
 *   DFPlayer 1: SoftwareSerial  RX=2  TX=3
 *   DFPlayer 2: SoftwareSerial  RX=4  TX=5
 *   DFPlayer 3: SoftwareSerial  RX=6  TX=7
 *   Button:     GPIO 0  (active LOW, internal pull-up)
 *   BEG LED:    GPIO 1  (ON when idle, OFF when playing)
 *   Free GPIO: 10, 20, 21
 *
 * Playing status is detected via DFPlayer serial events
 * (.available() / .readType()) rather than the BUSY pin.
 *
 * Pin safety (ESP32-C3 Super Mini):
 *   Avoid GPIO 8 (onboard LED / strapping) and GPIO 9 (BOOT button).
 *
 * Dependencies:
 *   - PubSubClient (Nick O'Leary)
 *   - ArduinoJson (Benoît Blanchon)
 *   - DFRobotDFPlayerMini
 *   - EspSoftwareSerial (Peter Lerup)
 *   - Preferences (built-in ESP32)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <secrets.h>

// ════════════════════════════════════════════════════════════════
//  CONFIGURATION
// ════════════════════════════════════════════════════════════════

// ─── Pin assignments ────────────────────────────────────────────
// DFPlayer 1 — SoftwareSerial
#define DFP1_RX    3
#define DFP1_TX    2

// DFPlayer 2 — SoftwareSerial
#define DFP2_RX    5
#define DFP2_TX    4

// DFPlayer 3 — SoftwareSerial
#define DFP3_RX    7
#define DFP3_TX    6

// Button & BEG status LED
#define PIN_BUTTON     0     // Active LOW with internal pull-up
#define PIN_BEG_LED    1     // ON when idle (not playing), OFF when playing

// ─── Defaults ───────────────────────────────────────────────────
#define DEFAULT_VOLUME   15
#define DEFAULT_TRACK    1
#define DEFAULT_FOLDER   1

// ─── Timing ─────────────────────────────────────────────────────
#define MQTT_RETRY_MS       5000
#define NAME_REQ_RETRY_MS  15000
#define WIFI_RETRY_MS      15000
#define WIFI_AP_DELAY_MS   30000

const char FIRMWARE_VERSION[] = "2.1.5";
#define DEVICE_TYPE "triple-audio-player"
#define HARDWARE_TARGET "esp32-c3"
#define HARDWARE_REVISION 1
#define HARDWARE_VERSION "v1.0"
#define OTA_REPOSITORY_OWNER "GeorgeHinch"
#define OTA_REPOSITORY_NAME "GeorgeHinch_Devices"

constexpr const char* CONFIG_NAMESPACE = "device_cfg";
constexpr const char* CONFIG_VERSION_KEY = "cfgver";
constexpr uint8_t CONFIG_VERSION = 1;

#include "DeviceOta.h"
#include "DeviceSerialSetup.h"

// ════════════════════════════════════════════════════════════════
//  ENUMS & STRUCTS
// ════════════════════════════════════════════════════════════════

enum PlayMode {
  MODE_LOOP_TRACK,
  MODE_SINGLE,
  MODE_LOOP_FOLDER
};

enum BegButtonAction {
  BEG_DISABLED = 0,
  BEG_PLAYER_1 = 1,
  BEG_PLAYER_2 = 2,
  BEG_PLAYER_3 = 3
};

struct DeviceSettings {
  char wifiSsid[33];
  char wifiPassword[65];
  bool mqttEnabled;
  char mqttBroker[128];
  uint16_t mqttPort;
  char mqttUser[65];
  char mqttPassword[65];
  char jmriChannel[65];
  uint8_t begButtonAction;
  bool globalBegEnabled;
};

// Per-player state
struct PlayerState {
  // Hardware
  DFRobotDFPlayerMini dfp;

  // JMRI local IDs (short form, e.g. "P1_MUTE")
  char idMute[12];
  char idPlay[12];
  char idPlaying[16];
  char idVol[12];
  char idMode[12];
  char idTrack[16];
  char idFolder[16];

  // JMRI full IDs (e.g. "AUDIOPLAY_AABBCCDDEEFF/P1_MUTE") — built at runtime
  char fullMute[40];
  char fullPlay[40];
  char fullPlaying[40];
  char fullVol[40];
  char fullMode[40];
  char fullTrack[40];
  char fullFolder[40];

  // User-defined names (from JMRI, cached in NVS)
  char nameMute[32];
  char namePlay[32];
  char namePlaying[32];
  char nameVol[32];
  char nameMode[32];
  char nameTrack[32];
  char nameFolder[32];

  // Playback state
  PlayMode mode;
  uint8_t  volume;
  uint8_t  track;
  uint8_t  folder;
  bool     muted;
  bool     playing;        // Software-tracked via serial events
  bool     playRequested;  // JMRI commanded play
  bool     stopRequested;  // JMRI commanded stop
  bool     needsRestart;   // Mode/track/folder changed while playing
};

// ════════════════════════════════════════════════════════════════
//  GLOBALS
// ════════════════════════════════════════════════════════════════

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
Preferences  prefs;
DeviceSettings settings;

// Device identity
char deviceId[24];   // "AUDIOPLAY_AABBCCDDEEFF"
char fullMac[18];    // "AA:BB:CC:DD:EE:FF"

// Serial ports for DFPlayers (all SoftwareSerial)
SoftwareSerial swSerial1(DFP1_RX, DFP1_TX);
SoftwareSerial swSerial2(DFP2_RX, DFP2_TX);
SoftwareSerial swSerial3(DFP3_RX, DFP3_TX);

// Player instances
PlayerState players[3];
bool dfpPresent[3] = { false, false, false };

// Timing
unsigned long lastMqttRetry   = 0;
unsigned long lastNameReq     = 0;
unsigned long lastWifiRetry   = 0;
unsigned long wifiAttemptAt   = 0;
bool          wifiAttempted   = false;
bool          namesReceived   = false;

// BEG button debounce
bool          lastButtonReading  = HIGH;
bool          debouncedButton    = HIGH;   // Debounced state
unsigned long lastDebounceTime   = 0;
const unsigned long DEBOUNCE_MS  = 50;

// BEG light — JMRI IDs and name (report-only light)
char begLocalId[8]  = "BEG";
char begFullId[40];             // "AUDIOPLAY_AABBCCDDEEFF/BEG"  — built at runtime
char begName[32]    = "";       // Cached user name from JMRI
bool begLedState    = true;     // true = LED on (idle), false = LED off (playing)

// ════════════════════════════════════════════════════════════════
//  DEVICE ID — MAC-based, zero-config
// ════════════════════════════════════════════════════════════════

void buildDeviceId() {
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "AUDIOPLAY_%02X%02X%02X%02X%02X%02X",
           (uint8_t)mac, (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  snprintf(fullMac, sizeof(fullMac), "%02X:%02X:%02X:%02X:%02X:%02X",
           (uint8_t)mac, (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
}

// Build the full JMRI ID: "AUDIOPLAY_AABBCCDDEEFF/P1_MUTE"
void buildFullId(char* dest, size_t len, const char* localId) {
  snprintf(dest, len, "%s/%s", deviceId, localId);
}

// ════════════════════════════════════════════════════════════════
//  PLAYER INITIALIZATION
// ════════════════════════════════════════════════════════════════

void initPlayerIds(int idx) {
  int pNum = idx + 1;  // 1-based for display
  PlayerState& p = players[idx];

  // Local IDs
  snprintf(p.idMute,    sizeof(p.idMute),    "P%d_MUTE",    pNum);
  snprintf(p.idPlay,    sizeof(p.idPlay),    "P%d_PLAY",    pNum);
  snprintf(p.idPlaying, sizeof(p.idPlaying), "P%d_PLAYING", pNum);
  snprintf(p.idVol,     sizeof(p.idVol),     "P%d_VOL",     pNum);
  snprintf(p.idMode,    sizeof(p.idMode),    "P%d_MODE",    pNum);
  snprintf(p.idTrack,   sizeof(p.idTrack),   "P%d_TRACK",   pNum);
  snprintf(p.idFolder,  sizeof(p.idFolder),  "P%d_FOLDER",  pNum);

  // Full JMRI IDs (device-scoped)
  buildFullId(p.fullMute,    sizeof(p.fullMute),    p.idMute);
  buildFullId(p.fullPlay,    sizeof(p.fullPlay),    p.idPlay);
  buildFullId(p.fullPlaying, sizeof(p.fullPlaying), p.idPlaying);
  buildFullId(p.fullVol,     sizeof(p.fullVol),     p.idVol);
  buildFullId(p.fullMode,    sizeof(p.fullMode),    p.idMode);
  buildFullId(p.fullTrack,   sizeof(p.fullTrack),   p.idTrack);
  buildFullId(p.fullFolder,  sizeof(p.fullFolder),  p.idFolder);

  // Defaults
  p.mode          = MODE_LOOP_TRACK;
  p.volume        = DEFAULT_VOLUME;
  p.track         = DEFAULT_TRACK;
  p.folder        = DEFAULT_FOLDER;
  p.muted         = false;
  p.playing       = false;
  p.playRequested = false;
  p.stopRequested = false;
  p.needsRestart  = false;

  // Clear names
  memset(p.nameMute,    0, sizeof(p.nameMute));
  memset(p.namePlay,    0, sizeof(p.namePlay));
  memset(p.namePlaying, 0, sizeof(p.namePlaying));
  memset(p.nameVol,     0, sizeof(p.nameVol));
  memset(p.nameMode,    0, sizeof(p.nameMode));
  memset(p.nameTrack,   0, sizeof(p.nameTrack));
  memset(p.nameFolder,  0, sizeof(p.nameFolder));
}

bool initDFPlayer(int idx, Stream& serial) {
  PlayerState& p = players[idx];
  Serial.printf("Initializing DFPlayer %d ...\n", idx + 1);

  if (!p.dfp.begin(serial, true, true)) {
    Serial.printf("  ERROR: DFPlayer %d init failed — check wiring/SD card\n", idx + 1);
    return false;
  }

  p.dfp.volume(p.volume);
  Serial.printf("  DFPlayer %d online, volume=%d\n", idx + 1, p.volume);
  return true;
}

// ════════════════════════════════════════════════════════════════
//  NVS — PERSIST SETTINGS & NAMES
// ════════════════════════════════════════════════════════════════

void savePlayerSettings(int idx) {
  PlayerState& p = players[idx];
  char key[16];

  prefs.begin("dfp_cfg", false);

  snprintf(key, sizeof(key), "vol%d",    idx); prefs.putUChar(key, p.volume);
  snprintf(key, sizeof(key), "mode%d",   idx); prefs.putUChar(key, (uint8_t)p.mode);
  snprintf(key, sizeof(key), "track%d",  idx); prefs.putUChar(key, p.track);
  snprintf(key, sizeof(key), "folder%d", idx); prefs.putUChar(key, p.folder);
  snprintf(key, sizeof(key), "mute%d",   idx); prefs.putBool(key, p.muted);

  prefs.end();
}

void loadPlayerSettings(int idx) {
  PlayerState& p = players[idx];
  char key[16];

  prefs.begin("dfp_cfg", true);  // read-only

  snprintf(key, sizeof(key), "vol%d",    idx); p.volume = prefs.getUChar(key, DEFAULT_VOLUME);
  snprintf(key, sizeof(key), "mode%d",   idx); p.mode   = (PlayMode)prefs.getUChar(key, MODE_LOOP_TRACK);
  snprintf(key, sizeof(key), "track%d",  idx); p.track  = prefs.getUChar(key, DEFAULT_TRACK);
  snprintf(key, sizeof(key), "folder%d", idx); p.folder = prefs.getUChar(key, DEFAULT_FOLDER);
  snprintf(key, sizeof(key), "mute%d",   idx); p.muted  = prefs.getBool(key, false);

  prefs.end();
}

void copySetting(char* destination, size_t length, const char* value) {
  if (!value) value = "";
  strncpy(destination, value, length - 1);
  destination[length - 1] = '\0';
}

void normalizeJmriChannel() {
  String channel(settings.jmriChannel);
  channel.trim();
  if (!channel.startsWith("/")) channel = "/" + channel;
  if (!channel.endsWith("/")) channel += "/";
  copySetting(settings.jmriChannel, sizeof(settings.jmriChannel), channel.c_str());
}

void normalizeMqttBroker() {
  String broker(settings.mqttBroker);
  broker.trim();
  if (broker.startsWith("mqtt://")) broker.remove(0, 7);
  if (broker.startsWith("mqtts://")) broker.remove(0, 8);
  int slash = broker.indexOf('/');
  if (slash >= 0) broker.remove(slash);
  copySetting(settings.mqttBroker, sizeof(settings.mqttBroker), broker.c_str());
}

void loadDeviceSettings() {
  prefs.begin(CONFIG_NAMESPACE, true);
  String value = prefs.getString("wifiSsid", WIFI_SSID);
  copySetting(settings.wifiSsid, sizeof(settings.wifiSsid), value.c_str());
  value = prefs.getString("wifiPass", WIFI_PASSWORD);
  copySetting(settings.wifiPassword, sizeof(settings.wifiPassword), value.c_str());
  settings.mqttEnabled = prefs.getBool("mqttEn", strlen(MQTT_BROKER) > 0);
  value = prefs.getString("mqttHost", MQTT_BROKER);
  copySetting(settings.mqttBroker, sizeof(settings.mqttBroker), value.c_str());
  settings.mqttPort = prefs.getUShort("mqttPort", MQTT_PORT);
  value = prefs.getString("mqttUser", MQTT_USER);
  copySetting(settings.mqttUser, sizeof(settings.mqttUser), value.c_str());
  value = prefs.getString("mqttPass", MQTT_PASS);
  copySetting(settings.mqttPassword, sizeof(settings.mqttPassword), value.c_str());
  value = prefs.getString("mqttPref", JMRI_CHANNEL);
  copySetting(settings.jmriChannel, sizeof(settings.jmriChannel), value.c_str());
  settings.begButtonAction = prefs.getUChar("begAction", BEG_DISABLED);
  settings.globalBegEnabled = prefs.getBool("begGlobal", true);
  prefs.end();
  normalizeJmriChannel();
  normalizeMqttBroker();
}

void saveDeviceSettings() {
  normalizeJmriChannel();
  normalizeMqttBroker();
  prefs.begin(CONFIG_NAMESPACE, false);
  prefs.putUInt(CONFIG_VERSION_KEY, CONFIG_VERSION);
  prefs.putString("wifiSsid", settings.wifiSsid);
  prefs.putString("wifiPass", settings.wifiPassword);
  prefs.putBool("mqttEn", settings.mqttEnabled);
  prefs.putString("mqttHost", settings.mqttBroker);
  prefs.putUShort("mqttPort", settings.mqttPort);
  prefs.putString("mqttUser", settings.mqttUser);
  prefs.putString("mqttPass", settings.mqttPassword);
  prefs.putString("mqttPref", settings.jmriChannel);
  prefs.putUChar("begAction", settings.begButtonAction);
  prefs.putBool("begGlobal", settings.globalBegEnabled);
  prefs.end();
}

void saveGlobalBegSetting() {
  prefs.begin(CONFIG_NAMESPACE, false);
  prefs.putBool("begGlobal", settings.globalBegEnabled);
  prefs.end();
}

void savePlayerName(const char* localId, const char* name) {
  char key[16];
  snprintf(key, sizeof(key), "n_%.12s", localId);
  prefs.begin("dfp_names", false);
  prefs.putString(key, name);
  prefs.end();
}

void loadPlayerNames(int idx) {
  PlayerState& p = players[idx];
  char key[16];

  prefs.begin("dfp_names", true);

  snprintf(key, sizeof(key), "n_%.12s", p.idMute);    prefs.getString(key, p.nameMute,    sizeof(p.nameMute));
  snprintf(key, sizeof(key), "n_%.12s", p.idPlay);    prefs.getString(key, p.namePlay,    sizeof(p.namePlay));
  snprintf(key, sizeof(key), "n_%.12s", p.idPlaying); prefs.getString(key, p.namePlaying, sizeof(p.namePlaying));
  snprintf(key, sizeof(key), "n_%.12s", p.idVol);     prefs.getString(key, p.nameVol,     sizeof(p.nameVol));
  snprintf(key, sizeof(key), "n_%.12s", p.idMode);    prefs.getString(key, p.nameMode,    sizeof(p.nameMode));
  snprintf(key, sizeof(key), "n_%.12s", p.idTrack);   prefs.getString(key, p.nameTrack,   sizeof(p.nameTrack));
  snprintf(key, sizeof(key), "n_%.12s", p.idFolder);  prefs.getString(key, p.nameFolder,  sizeof(p.nameFolder));

  prefs.end();
}

// ════════════════════════════════════════════════════════════════
//  MODE / STATE HELPERS
// ════════════════════════════════════════════════════════════════

const char* modeToString(PlayMode m) {
  switch (m) {
    case MODE_LOOP_TRACK:  return "LOOP_TRACK";
    case MODE_SINGLE:      return "SINGLE";
    case MODE_LOOP_FOLDER: return "LOOP_FOLDER";
    default:               return "LOOP_TRACK";
  }
}

PlayMode stringToMode(const char* s) {
  if (strcmp(s, "SINGLE")      == 0) return MODE_SINGLE;
  if (strcmp(s, "LOOP_FOLDER") == 0) return MODE_LOOP_FOLDER;
  return MODE_LOOP_TRACK;  // default
}

// ════════════════════════════════════════════════════════════════
//  PLAYBACK CONTROL
// ════════════════════════════════════════════════════════════════

void startPlayback(int idx) {
  PlayerState& p = players[idx];

  if (!dfpPresent[idx]) {
    p.playRequested = false;
    p.needsRestart = false;
    Serial.printf("P%d: play ignored (DFPlayer not detected)\n", idx + 1);
    return;
  }

  // Apply mute state (volume 0 vs stored volume)
  p.dfp.volume(p.muted ? 0 : p.volume);

  switch (p.mode) {
    case MODE_LOOP_TRACK:
      Serial.printf("P%d: loop track %d\n", idx + 1, p.track);
      p.dfp.loop(p.track);
      break;

    case MODE_SINGLE:
      Serial.printf("P%d: play track %d (single)\n", idx + 1, p.track);
      p.dfp.play(p.track);
      break;

    case MODE_LOOP_FOLDER:
      Serial.printf("P%d: loop folder %d\n", idx + 1, p.folder);
      p.dfp.loopFolder(p.folder);
      break;
  }

  p.playRequested = false;
  p.needsRestart  = false;

  // Update playing state and notify JMRI
  if (!p.playing) {
    p.playing = true;
    publishSensorState(p.fullPlaying, true);
    publishTurnoutState(p.fullPlay, true);
    Serial.printf("P%d: PLAYING\n", idx + 1);
  }
}

void stopPlayback(int idx) {
  PlayerState& p = players[idx];
  if (!dfpPresent[idx]) {
    p.stopRequested = false;
    p.playing = false;
    return;
  }
  Serial.printf("P%d: stop\n", idx + 1);
  p.dfp.stop();
  p.stopRequested = false;

  // Update playing state and notify JMRI
  if (p.playing) {
    p.playing = false;
    publishSensorState(p.fullPlaying, false);
    publishTurnoutState(p.fullPlay, false);
    Serial.printf("P%d: STOPPED\n", idx + 1);
  }
}

// ════════════════════════════════════════════════════════════════
//  MQTT TOPIC BUILDERS
// ════════════════════════════════════════════════════════════════

// Build full topic: "/trains/track/turnout/AUDIOPLAY_AABBCCDDEEFF/P1_MUTE"
String buildTopic(const char* prefix, const char* fullId) {
  String t = settings.jmriChannel;
  t += prefix;
  t += fullId;
  return t;
}

// ════════════════════════════════════════════════════════════════
//  MQTT PUBLISH HELPERS
// ════════════════════════════════════════════════════════════════

void publishTurnoutState(const char* fullId, bool state) {
  String topic = buildTopic(TOPIC_TURNOUT, fullId);
  const char* payload = state ? "THROWN" : "CLOSED";
  mqtt.publish(topic.c_str(), payload, true);
  Serial.printf("  PUB  %-45s  %s\n", topic.c_str(), payload);
}

void publishSensorState(const char* fullId, bool active) {
  String topic = buildTopic(TOPIC_SENSOR, fullId);
  const char* payload = active ? "ACTIVE" : "INACTIVE";
  mqtt.publish(topic.c_str(), payload, true);
  Serial.printf("  PUB  %-45s  %s\n", topic.c_str(), payload);
}

void publishMemoryState(const char* fullId, const char* value) {
  String topic = buildTopic(TOPIC_MEMORY, fullId);
  mqtt.publish(topic.c_str(), value, true);
  Serial.printf("  PUB  %-45s  %s\n", topic.c_str(), value);
}

void publishLightState(const char* fullId, bool on) {
  String topic = buildTopic(TOPIC_LIGHT, fullId);
  const char* payload = on ? "ON" : "OFF";
  mqtt.publish(topic.c_str(), payload, true);
  Serial.printf("  PUB  %-45s  %s\n", topic.c_str(), payload);
}

// Publish all state for one player
void publishAllState(int idx) {
  PlayerState& p = players[idx];

  publishTurnoutState(p.fullMute, p.muted);
  publishTurnoutState(p.fullPlay, p.playing);
  publishSensorState(p.fullPlaying, p.playing);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", p.volume);
  publishMemoryState(p.fullVol, buf);

  publishMemoryState(p.fullMode, modeToString(p.mode));

  snprintf(buf, sizeof(buf), "%d", p.track);
  publishMemoryState(p.fullTrack, buf);

  snprintf(buf, sizeof(buf), "%d", p.folder);
  publishMemoryState(p.fullFolder, buf);
}

// ════════════════════════════════════════════════════════════════
//  MQTT DISCOVERY
// ════════════════════════════════════════════════════════════════

void publishDiscovery() {
  JsonDocument doc;
  doc["device"]  = deviceId;
  doc["mac"]     = fullMac;
  doc["type"]    = "audio_controller";
  doc["players"] = 3;
  doc["firmware"] = FIRMWARE_VERSION;

  JsonArray outputs = doc["outputs"].to<JsonArray>();

  for (int i = 0; i < 3; i++) {
    PlayerState& p = players[i];

    // Turnout: MUTE
    JsonObject tMute = outputs.add<JsonObject>();
    tMute["type"]  = "turnout";
    tMute["id"]    = p.fullMute;
    tMute["state"] = p.muted ? "THROWN" : "CLOSED";
    if (strlen(p.nameMute) > 0) tMute["name"] = p.nameMute;

    // Turnout: PLAY
    JsonObject tPlay = outputs.add<JsonObject>();
    tPlay["type"]  = "turnout";
    tPlay["id"]    = p.fullPlay;
    tPlay["state"] = p.playing ? "THROWN" : "CLOSED";
    if (strlen(p.namePlay) > 0) tPlay["name"] = p.namePlay;

    // Sensor: PLAYING
    JsonObject sPlaying = outputs.add<JsonObject>();
    sPlaying["type"]  = "sensor";
    sPlaying["id"]    = p.fullPlaying;
    sPlaying["state"] = p.playing ? "ACTIVE" : "INACTIVE";
    if (strlen(p.namePlaying) > 0) sPlaying["name"] = p.namePlaying;

    // Memory: VOLUME
    JsonObject mVol = outputs.add<JsonObject>();
    mVol["type"]  = "memory";
    mVol["id"]    = p.fullVol;
    char volBuf[4];
    snprintf(volBuf, sizeof(volBuf), "%d", p.volume);
    mVol["state"] = volBuf;
    if (strlen(p.nameVol) > 0) mVol["name"] = p.nameVol;

    // Memory: MODE
    JsonObject mMode = outputs.add<JsonObject>();
    mMode["type"]  = "memory";
    mMode["id"]    = p.fullMode;
    mMode["state"] = modeToString(p.mode);
    if (strlen(p.nameMode) > 0) mMode["name"] = p.nameMode;

    // Memory: TRACK
    JsonObject mTrack = outputs.add<JsonObject>();
    mTrack["type"]  = "memory";
    mTrack["id"]    = p.fullTrack;
    char trkBuf[4];
    snprintf(trkBuf, sizeof(trkBuf), "%d", p.track);
    mTrack["state"] = trkBuf;
    if (strlen(p.nameTrack) > 0) mTrack["name"] = p.nameTrack;

    // Memory: FOLDER
    JsonObject mFolder = outputs.add<JsonObject>();
    mFolder["type"]  = "memory";
    mFolder["id"]    = p.fullFolder;
    char fldBuf[4];
    snprintf(fldBuf, sizeof(fldBuf), "%d", p.folder);
    mFolder["state"] = fldBuf;
    if (strlen(p.nameFolder) > 0) mFolder["name"] = p.nameFolder;
  }

  // Light: BEG status LED (report-only)
  JsonObject lBeg = outputs.add<JsonObject>();
  lBeg["type"]  = "light";
  lBeg["id"]    = begFullId;
  lBeg["state"] = begLedState ? "ON" : "OFF";
  if (strlen(begName) > 0) lBeg["name"] = begName;

  // Serialize & publish
  size_t jsonLen = measureJson(doc) + 1;
  char* json = (char*)malloc(jsonLen);
  if (!json) {
    Serial.println(F("Discovery malloc failed"));
    return;
  }

  // Grow MQTT buffer if needed
  if (jsonLen > (size_t)mqtt.getBufferSize()) {
    mqtt.setBufferSize(jsonLen + 128);
  }

  serializeJson(doc, json, jsonLen);

  char topic[80];
  snprintf(topic, sizeof(topic), "%s%s%s", settings.jmriChannel, DISCOVERY_TOPIC, deviceId);
  mqtt.publish(topic, json, true);  // retained
  Serial.printf("Discovery published (%u bytes)\n", (unsigned)jsonLen);

  free(json);
}

// ════════════════════════════════════════════════════════════════
//  MQTT NAME SYNC
// ════════════════════════════════════════════════════════════════

void requestNames() {
  // Build JSON: {"outputs":["AUDIOPLAY_AABBCCDDEEFF/P1_MUTE","AUDIOPLAY_AABBCCDDEEFF/P1_PLAY",...]}
  JsonDocument doc;
  JsonArray arr = doc["outputs"].to<JsonArray>();

  for (int i = 0; i < 3; i++) {
    PlayerState& p = players[i];
    arr.add(p.fullMute);
    arr.add(p.fullPlay);
    arr.add(p.fullPlaying);
    arr.add(p.fullVol);
    arr.add(p.fullMode);
    arr.add(p.fullTrack);
    arr.add(p.fullFolder);
  }

  // BEG light
  arr.add(begFullId);

  size_t len = measureJson(doc) + 1;
  char* json = (char*)malloc(len);
  if (!json) return;

  if (len > (size_t)mqtt.getBufferSize()) {
    mqtt.setBufferSize(len + 64);
  }

  serializeJson(doc, json, len);

  char topic[80];
  snprintf(topic, sizeof(topic), "%s%s%s", settings.jmriChannel, NAMES_REQ_TOPIC, deviceId);
  mqtt.publish(topic, json);
  Serial.println(F("Name-sync request sent"));

  free(json);
  lastNameReq = millis();
}

// Find which player and which field a JMRI ID belongs to.
// Returns pointer to the name buffer, or NULL if not found.
// Also sets *playerIdx if non-null.
char* findNameBuffer(const char* id, int* playerIdx) {
  for (int i = 0; i < 3; i++) {
    PlayerState& p = players[i];
    // Match against both full ID and local ID
    if (strcmp(id, p.fullMute) == 0 || strcmp(id, p.idMute) == 0) {
      if (playerIdx) *playerIdx = i;
      return p.nameMute;
    }
    if (strcmp(id, p.fullPlay) == 0 || strcmp(id, p.idPlay) == 0) {
      if (playerIdx) *playerIdx = i;
      return p.namePlay;
    }
    if (strcmp(id, p.fullPlaying) == 0 || strcmp(id, p.idPlaying) == 0) {
      if (playerIdx) *playerIdx = i;
      return p.namePlaying;
    }
    if (strcmp(id, p.fullVol) == 0 || strcmp(id, p.idVol) == 0) {
      if (playerIdx) *playerIdx = i;
      return p.nameVol;
    }
    if (strcmp(id, p.fullMode) == 0 || strcmp(id, p.idMode) == 0) {
      if (playerIdx) *playerIdx = i;
      return p.nameMode;
    }
    if (strcmp(id, p.fullTrack) == 0 || strcmp(id, p.idTrack) == 0) {
      if (playerIdx) *playerIdx = i;
      return p.nameTrack;
    }
    if (strcmp(id, p.fullFolder) == 0 || strcmp(id, p.idFolder) == 0) {
      if (playerIdx) *playerIdx = i;
      return p.nameFolder;
    }
  }

  // BEG light
  if (strcmp(id, begFullId) == 0 || strcmp(id, begLocalId) == 0) {
    if (playerIdx) *playerIdx = -1;
    return begName;
  }

  return NULL;
}

// Store a name into the correct buffer + NVS
void storeName(const char* id, const char* name) {
  int pIdx = -1;
  char* buf = findNameBuffer(id, &pIdx);
  if (!buf) return;

  strncpy(buf, name, 31);
  buf[31] = '\0';

  // Extract local ID for NVS key (part after the slash, or the id itself)
  const char* localId = id;
  const char* slash = strchr(id, '/');
  if (slash) localId = slash + 1;

  savePlayerName(localId, name);
  Serial.printf("  Name: %s → \"%s\"\n", id, name);
}

// ════════════════════════════════════════════════════════════════
//  MQTT CALLBACK
// ════════════════════════════════════════════════════════════════

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  String t(topic);
  String otaMessage(msg);
  otaMessage.trim();
  otaMessage.toUpperCase();
  bool safeForOta = true;
  for (int i = 0; i < 3; ++i) safeForOta = safeForOta && !players[i].playing;
  if (DeviceOta::handleMqtt(mqtt, settings.mqttEnabled, safeForOta, t, otaMessage)) return;

  // Global JMRI BEG Light: OFF overrides the local ready LED.
  String globalBegTopic = buildTopic(TOPIC_LIGHT, "BEG");
  if (t == globalBegTopic) {
    bool enabled = strcmp(msg, "OFF") != 0;
    if (enabled != settings.globalBegEnabled) {
      settings.globalBegEnabled = enabled;
      saveGlobalBegSetting();
      updateBegLed();
      Serial.printf("Global BEG LEDs: %s\n", enabled ? "enabled" : "disabled");
    }
    return;
  }

  // ── Name response (bulk JSON) ──
  if (t.indexOf(NAMES_RESP_TOPIC) >= 0 && t.indexOf(deviceId) >= 0) {
    JsonDocument doc;
    if (deserializeJson(doc, msg)) return;

    for (JsonPair kv : doc.as<JsonObject>()) {
      storeName(kv.key().c_str(), kv.value().as<const char*>());
    }
    namesReceived = true;
    publishDiscovery();  // Re-publish with names
    return;
  }

  // ── Name push (single JSON) ──
  if (t.indexOf(NAMES_PUSH_TOPIC) >= 0 && t.indexOf(deviceId) >= 0) {
    JsonDocument doc;
    if (deserializeJson(doc, msg)) return;

    const char* objId = doc["id"];
    const char* name  = doc["name"];
    if (objId && name) {
      storeName(objId, name);
      publishDiscovery();  // Re-publish with updated name
    }
    return;
  }

  // ── Turnout commands (MUTE, PLAY) ──
  if (t.indexOf(TOPIC_TURNOUT) >= 0) {
    for (int i = 0; i < 3; i++) {
      PlayerState& p = players[i];

      // MUTE turnout
      String muteTopic = buildTopic(TOPIC_TURNOUT, p.fullMute);
      if (t == muteTopic) {
        bool newMuted = (strcmp(msg, "THROWN") == 0);
        if (newMuted != p.muted) {
          p.muted = newMuted;
          // Apply mute immediately: set volume to 0 or restore
          if (dfpPresent[i]) p.dfp.volume(p.muted ? 0 : p.volume);
          publishTurnoutState(p.fullMute, p.muted);
          savePlayerSettings(i);
          Serial.printf("P%d: mute=%s\n", i + 1, p.muted ? "ON" : "OFF");
        }
        return;
      }

      // PLAY turnout
      String playTopic = buildTopic(TOPIC_TURNOUT, p.fullPlay);
      if (t == playTopic) {
        bool wantPlay = (strcmp(msg, "THROWN") == 0);

        if (wantPlay && !p.playing) {
          p.playRequested = true;
        } else if (!wantPlay && p.playing) {
          p.stopRequested = true;
        }
        // Don't publish state echo here — startPlayback() and
        // stopPlayback() handle state updates and JMRI notification.
        return;
      }
    }
    return;
  }

  // ── Memory commands (VOL, MODE, TRACK, FOLDER) ──
  if (t.indexOf(TOPIC_MEMORY) >= 0) {
    for (int i = 0; i < 3; i++) {
      PlayerState& p = players[i];

      // VOLUME
      String volTopic = buildTopic(TOPIC_MEMORY, p.fullVol);
      if (t == volTopic) {
        int newVol = atoi(msg);
        if (newVol < 0) newVol = 0;
        if (newVol > 30) newVol = 30;
        if ((uint8_t)newVol != p.volume) {
          p.volume = (uint8_t)newVol;
          if (!p.muted) {
            if (dfpPresent[i]) p.dfp.volume(p.volume);
          }
          char buf[4];
          snprintf(buf, sizeof(buf), "%d", p.volume);
          publishMemoryState(p.fullVol, buf);
          savePlayerSettings(i);
          Serial.printf("P%d: volume=%d\n", i + 1, p.volume);
        }
        return;
      }

      // MODE
      String modeTopic = buildTopic(TOPIC_MEMORY, p.fullMode);
      if (t == modeTopic) {
        PlayMode newMode = stringToMode(msg);
        if (newMode != p.mode) {
          p.mode = newMode;
          publishMemoryState(p.fullMode, modeToString(p.mode));
          savePlayerSettings(i);
          Serial.printf("P%d: mode=%s\n", i + 1, modeToString(p.mode));
          // If currently playing, restart with new mode
          if (p.playing) {
            p.needsRestart = true;
          }
        }
        return;
      }

      // TRACK
      String trackTopic = buildTopic(TOPIC_MEMORY, p.fullTrack);
      if (t == trackTopic) {
        int newTrack = atoi(msg);
        if (newTrack < 1) newTrack = 1;
        if (newTrack > 255) newTrack = 255;
        if ((uint8_t)newTrack != p.track) {
          p.track = (uint8_t)newTrack;
          char buf[4];
          snprintf(buf, sizeof(buf), "%d", p.track);
          publishMemoryState(p.fullTrack, buf);
          savePlayerSettings(i);
          Serial.printf("P%d: track=%d\n", i + 1, p.track);
          // If currently playing in a track-based mode, restart
          if (p.playing && p.mode != MODE_LOOP_FOLDER) {
            p.needsRestart = true;
          }
        }
        return;
      }

      // FOLDER
      String folderTopic = buildTopic(TOPIC_MEMORY, p.fullFolder);
      if (t == folderTopic) {
        int newFolder = atoi(msg);
        if (newFolder < 1) newFolder = 1;
        if (newFolder > 99) newFolder = 99;
        if ((uint8_t)newFolder != p.folder) {
          p.folder = (uint8_t)newFolder;
          char buf[4];
          snprintf(buf, sizeof(buf), "%d", p.folder);
          publishMemoryState(p.fullFolder, buf);
          savePlayerSettings(i);
          Serial.printf("P%d: folder=%d\n", i + 1, p.folder);
          // If currently playing folder-loop mode, restart
          if (p.playing && p.mode == MODE_LOOP_FOLDER) {
            p.needsRestart = true;
          }
        }
        return;
      }
    }
    return;
  }
}

// ════════════════════════════════════════════════════════════════
//  WiFi
// ════════════════════════════════════════════════════════════════

void setupWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (settings.wifiSsid[0] == '\0') {
    startConfigAccessPoint();
    return;
  }

  unsigned long now = millis();
  if (!wifiAttempted) {
    wifiAttempted = true;
    wifiAttemptAt = now;
  }

  if (lastWifiRetry == 0 || now - lastWifiRetry >= WIFI_RETRY_MS) {
    lastWifiRetry = now;
    Serial.printf("Connecting to WiFi \"%s\"\n", settings.wifiSsid);
    WiFi.begin(settings.wifiSsid, settings.wifiPassword);
  }

  if (now - wifiAttemptAt >= WIFI_AP_DELAY_MS) startConfigAccessPoint();
}

// ════════════════════════════════════════════════════════════════
//  MQTT CONNECTION
// ════════════════════════════════════════════════════════════════

void mqttConnect() {
  if (!settings.mqttEnabled || settings.mqttBroker[0] == '\0') return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;
  if (millis() - lastMqttRetry < MQTT_RETRY_MS) return;
  lastMqttRetry = millis();

  Serial.print("MQTT connecting...");
  String clientId(deviceId);

  bool ok;
  if (strlen(settings.mqttUser) > 0) {
    ok = mqtt.connect(clientId.c_str(), settings.mqttUser, settings.mqttPassword);
  } else {
    ok = mqtt.connect(clientId.c_str());
  }

  if (!ok) {
    Serial.printf(" failed (rc=%d)\n", mqtt.state());
    return;
  }

  Serial.println(" connected");

  // Subscribe to all topics under our channel
  String sub = String(settings.jmriChannel) + "#";
  mqtt.subscribe(sub.c_str());
  Serial.printf("Subscribed: %s\n", sub.c_str());

  // Publish discovery manifest
  publishDiscovery();

  // Publish current state for all players
  for (int i = 0; i < 3; i++) {
    publishAllState(i);
  }

  // Publish BEG light state
  publishLightState(begFullId, begLedState);

  // Request names from JMRI
  namesReceived = false;
  requestNames();
}

// ════════════════════════════════════════════════════════════════
//  SERIAL EVENT PROCESSING — playing status via .available()/.readType()
// ════════════════════════════════════════════════════════════════

void processDFPlayerEvents() {
  for (int i = 0; i < 3; i++) {
    PlayerState& p = players[i];

    if (!dfpPresent[i]) continue;

    if (!p.dfp.available()) continue;

    uint8_t type  = p.dfp.readType();
    int     value = p.dfp.read();

    switch (type) {
      case DFPlayerPlayFinished:
        Serial.printf("P%d: track %d finished\n", i + 1, value);
        // In SINGLE mode, track finished = playback done
        if (p.mode == MODE_SINGLE && p.playing) {
          p.playing = false;
          publishSensorState(p.fullPlaying, false);
          publishTurnoutState(p.fullPlay, false);
          Serial.printf("P%d: STOPPED (single track complete)\n", i + 1);
        }
        // In LOOP_TRACK and LOOP_FOLDER modes, the DFPlayer restarts
        // automatically — playing state stays true.
        break;

      case DFPlayerCardRemoved:
        Serial.printf("P%d: SD card removed!\n", i + 1);
        if (p.playing) {
          p.playing = false;
          publishSensorState(p.fullPlaying, false);
          publishTurnoutState(p.fullPlay, false);
        }
        break;

      case DFPlayerError:
        Serial.printf("P%d: error (code %d)\n", i + 1, value);
        // On error, mark as stopped
        if (p.playing) {
          p.playing = false;
          publishSensorState(p.fullPlaying, false);
          publishTurnoutState(p.fullPlay, false);
          Serial.printf("P%d: STOPPED (error)\n", i + 1);
        }
        break;

      case DFPlayerCardOnline:
        Serial.printf("P%d: SD card online\n", i + 1);
        break;

      default:
        Serial.printf("P%d: serial event type=%d value=%d\n", i + 1, type, value);
        break;
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  PLAYBACK STATE MACHINE — runs each loop()
// ════════════════════════════════════════════════════════════════

void updatePlayback() {
  for (int i = 0; i < 3; i++) {
    PlayerState& p = players[i];

    // Handle restart (mode/track/folder changed while playing)
    if (p.needsRestart && p.playing) {
      startPlayback(i);
      continue;
    }

    // Handle play request from JMRI
    if (p.playRequested) {
      startPlayback(i);
      continue;
    }

    // Handle stop request from JMRI
    if (p.stopRequested) {
      stopPlayback(i);
      continue;
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  BEG LED — ready indicator with global JMRI override
// ════════════════════════════════════════════════════════════════

void updateBegLed() {
  bool anyPlaying = false;
  for (int i = 0; i < 3; i++) anyPlaying = anyPlaying || players[i].playing;
  bool newState = settings.globalBegEnabled && !anyPlaying;

  if (newState != begLedState) {
    begLedState = newState;
    digitalWrite(PIN_BEG_LED, begLedState ? HIGH : LOW);
    publishLightState(begFullId, begLedState);
    Serial.printf("BEG LED: %s\n", begLedState ? "ON (idle)" : "OFF (playing)");
  }
}

// ════════════════════════════════════════════════════════════════
//  BUTTON — triggers the portal-selected player
// ════════════════════════════════════════════════════════════════

void readButton() {
  bool reading = digitalRead(PIN_BUTTON);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  lastButtonReading = reading;

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    // Falling edge on debounced signal — button just pressed (active LOW)
    if (reading == LOW && debouncedButton == HIGH) {
      int playerIndex = (int)settings.begButtonAction - 1;
      if (playerIndex < 0 || playerIndex > 2) {
        Serial.println(F("Button: disabled"));
        debouncedButton = reading;
        return;
      }
      PlayerState& p = players[playerIndex];

      if (!dfpPresent[playerIndex]) {
        Serial.printf("Button: P%d not detected\n", playerIndex + 1);
        debouncedButton = reading;
        return;
      }

      if (!p.playing) {
        Serial.printf("Button: triggering P%d\n", playerIndex + 1);
        p.playRequested = true;
      } else {
        Serial.printf("Button: stopping P%d\n", playerIndex + 1);
        p.stopRequested = true;
      }
    }
    debouncedButton = reading;
  }
}

// ════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n═══ Triple DFPlayer JMRI Controller ═══"));

  // ── Device identity ──
  buildDeviceId();
  Serial.printf("Device: %s  MAC: %s\n", deviceId, fullMac);
  Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);

  // ── Initialize player IDs and defaults ──
  for (int i = 0; i < 3; i++) {
    initPlayerIds(i);
  }

  // ── Build BEG light full ID ──
  snprintf(begFullId, sizeof(begFullId), "%s/%s", deviceId, begLocalId);

  // ── Button & BEG LED pins ──
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BEG_LED, OUTPUT);
  digitalWrite(PIN_BEG_LED, HIGH);  // LED on at boot (idle)

  // ── Device and portal settings ──
  loadDeviceSettings();
  DeviceOta::begin(settings.jmriChannel, deviceId);
  bool forceConfigPortal = digitalRead(PIN_BUTTON) == LOW || settings.wifiSsid[0] == '\0';
  beginConfigPortal(forceConfigPortal);

  // ── Load NVS settings ──
  for (int i = 0; i < 3; i++) {
    loadPlayerSettings(i);
    loadPlayerNames(i);
    Serial.printf("P%d: vol=%d mode=%s track=%d folder=%d mute=%s\n",
                  i + 1, players[i].volume, modeToString(players[i].mode),
                  players[i].track, players[i].folder,
                  players[i].muted ? "ON" : "OFF");
  }

  // ── Load BEG light name from NVS ──
  {
    char key[16];
    snprintf(key, sizeof(key), "n_%.12s", begLocalId);
    prefs.begin("dfp_names", true);
    prefs.getString(key, begName, sizeof(begName));
    prefs.end();
  }

  // ── DFPlayer serial ports (all SoftwareSerial) ──
  swSerial1.begin(9600);
  swSerial2.begin(9600);
  swSerial3.begin(9600);

  // Allow modules to power up
  delay(1000);

  // ── Initialize DFPlayer modules ──
  dfpPresent[0] = initDFPlayer(0, swSerial1);
  dfpPresent[1] = initDFPlayer(1, swSerial2);
  dfpPresent[2] = initDFPlayer(2, swSerial3);

  for (int i = 0; i < 3; i++) {
    if (!dfpPresent[i]) {
      Serial.printf("WARNING: DFPlayer %d offline — continuing with remaining players\n", i + 1);
    }
  }

  // ── WiFi ──
  setupWiFi();

  // ── MQTT ──
  mqtt.setServer(settings.mqttBroker, settings.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(2048);  // Discovery manifest is large
  mqttConnect();

  Serial.println(F("═══ Setup complete ═══\n"));
}

// ════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════

void loop() {
  handleConfigPortal();
  DeviceSerialSetup::service(settings.wifiSsid, sizeof(settings.wifiSsid),
                             settings.wifiPassword, sizeof(settings.wifiPassword),
                             saveDeviceSettings, startConfigAccessPoint);

  // WiFi reconnect
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  } else if (wifiAttempted) {
    wifiAttempted = false;
    lastWifiRetry = 0;
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  }

  // MQTT reconnect
  if (settings.mqttEnabled && WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
    mqttConnect();
  }
  if (mqtt.connected()) mqtt.loop();

  // Process DFPlayer serial events (playing status detection)
  processDFPlayerEvents();

  // Read button input
  readButton();

  // Execute play/stop/restart commands
  updatePlayback();

  // Sync BEG LED with player state
  updateBegLed();

  // Retry name request if JMRI hasn't responded yet
  if (mqtt.connected() && !namesReceived && (millis() - lastNameReq > NAME_REQ_RETRY_MS)) {
    requestNames();
  }

  bool safeForOta = true;
  for (int i = 0; i < 3; ++i) safeForOta = safeForOta && !players[i].playing;
  DeviceOta::service(mqtt, settings.mqttEnabled, safeForOta);
}
