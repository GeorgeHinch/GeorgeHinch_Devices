#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_VL53L0X.h>
#include <esp_mac.h>

#include "config.h"
#include "secrets.h"

#define DEVICE_TYPE "motor-controller"
#define HARDWARE_TARGET "esp32-c3"
#define HARDWARE_REVISION 1
#define HARDWARE_VERSION "v1.0"
#define OTA_REPOSITORY_OWNER "GeorgeHinch"
#define OTA_REPOSITORY_NAME "GeorgeHinch_Devices"

const char FIRMWARE_VERSION[] = "1.1.6";
const char CONFIG_NAMESPACE[] = "device_cfg";
const uint32_t CONFIG_VERSION = 1;

#include "DeviceOta.h"
#include "DeviceSerialSetup.h"

WiFiClient networkClient;
PubSubClient mqtt(networkClient);
Preferences preferences;
Adafruit_VL53L0X rangeSensors[SENSOR_COUNT];

char deviceId[24];
char fullMac[18];
char mqttHost[128];

struct DeviceSettings {
  char wifiSsid[33];
  char wifiPassword[65];
  bool mqttEnabled = true;
  char mqttBroker[128];
  uint16_t mqttPort = 1883;
  char mqttUser[65];
  char mqttPassword[65];
  char jmriChannel[65];
  bool linkMotorGroups = DEFAULT_LINK_MOTOR_GROUPS;
  bool holdWhenStopped = DEFAULT_HOLD_WHEN_STOPPED;
  bool stopOnConnectionLoss = DEFAULT_STOP_MOTORS_ON_CONNECTION_LOSS;
  BegButtonAction begButtonAction = DEFAULT_BEG_BUTTON_ACTION;
  bool begButtonRunIndefinitely = DEFAULT_BEG_BUTTON_RUN_INDEFINITELY;
  uint16_t begButtonRunSeconds = DEFAULT_BEG_BUTTON_RUN_SECONDS;
  bool globalBegLedsEnabled = true;
  SensorControlMode sensorControlMode = DEFAULT_SENSOR_CONTROL_MODE;
  MotorGroupTarget sensorMotorTarget = DEFAULT_SENSOR_MOTOR_TARGET;
  uint16_t sensorRunSeconds = DEFAULT_SENSOR_RUN_SECONDS;
  uint16_t sensorClearHoldMs = DEFAULT_SENSOR_CLEAR_HOLD_MS;
  uint16_t sensorSamplePeriodMs = DEFAULT_SENSOR_SAMPLE_PERIOD_MS;
  uint16_t occupiedHysteresisMm = DEFAULT_OCCUPIED_HYSTERESIS_MM;
};

DeviceSettings settings;

struct MotorState {
  bool continuous = false;
  bool finiteMove = false;
  bool reverse = false;
  bool finiteReverse = false;
  bool everStepped = false;
  uint8_t phase = 0;
  uint16_t speed = DEFAULT_SPEED_STEPS_PER_SECOND;
  uint32_t nextStepUs = 0;
  uint32_t remainingSteps = 0;
  int32_t position = 0;
};

struct RangeState {
  bool present = false;
  bool occupied = false;
  int16_t distanceMm = -1;
  int16_t lastPublishedMm = -32768;
  uint16_t thresholdMm = DEFAULT_OCCUPIED_THRESHOLD_MM;
  uint32_t lastPublishMs = 0;
};

MotorState motors[MOTOR_COUNT];
RangeState ranges[SENSOR_COUNT];

enum ObjectKind : uint8_t { OBJ_TURNOUT, OBJ_SENSOR, OBJ_MEMORY, OBJ_LIGHT };

struct ObjectInfo {
  ObjectKind kind;
  const char* localId;
  char fullId[40];
  char name[49];
};

enum ObjectIndex : uint8_t {
  O_M1_RUN, O_M1_DIR, O_M1_MOVING, O_M1_SPEED, O_M1_MOVE, O_M1_POS,
  O_M2_RUN, O_M2_DIR, O_M2_MOVING, O_M2_SPEED, O_M2_MOVE, O_M2_POS,
  O_S1_OCCUPIED, O_S1_MM, O_S1_THRESHOLD,
  O_S2_OCCUPIED, O_S2_MM, O_S2_THRESHOLD,
  O_BEG,
  OBJECT_COUNT
};

ObjectInfo objects[OBJECT_COUNT] = {
  {OBJ_TURNOUT, "M1_RUN", {}, {}}, {OBJ_TURNOUT, "M1_DIR", {}, {}},
  {OBJ_SENSOR, "M1_MOVING", {}, {}}, {OBJ_MEMORY, "M1_SPEED", {}, {}},
  {OBJ_MEMORY, "M1_MOVE", {}, {}}, {OBJ_MEMORY, "M1_POS", {}, {}},
  {OBJ_TURNOUT, "M2_RUN", {}, {}}, {OBJ_TURNOUT, "M2_DIR", {}, {}},
  {OBJ_SENSOR, "M2_MOVING", {}, {}}, {OBJ_MEMORY, "M2_SPEED", {}, {}},
  {OBJ_MEMORY, "M2_MOVE", {}, {}}, {OBJ_MEMORY, "M2_POS", {}, {}},
  {OBJ_SENSOR, "S1_OCCUPIED", {}, {}}, {OBJ_MEMORY, "S1_MM", {}, {}},
  {OBJ_MEMORY, "S1_THRESHOLD", {}, {}}, {OBJ_SENSOR, "S2_OCCUPIED", {}, {}},
  {OBJ_MEMORY, "S2_MM", {}, {}}, {OBJ_MEMORY, "S2_THRESHOLD", {}, {}},
  {OBJ_LIGHT, "BEG", {}, {}}
};

uint8_t shiftByte = 0;
bool begLedState = false;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
bool setupButtonDown = false;
bool mqttWasConnected = false;
bool namesReceived = false;
uint32_t lastButtonChangeMs = 0;
uint32_t setupButtonHoldStartMs = 0;
uint32_t lastWiFiRetryMs = 0;
uint32_t lastMqttRetryMs = 0;
uint32_t lastNameRequestMs = 0;
bool sensorAutomationRunning = false;
int8_t sensorAutomationEntry = -1;
int8_t sensorAutomationExit = -1;
uint32_t sensorAutomationStartedMs = 0;
uint32_t sensorAutomationStopAtMs = 0;
uint32_t sensorAutomationExitClearStartMs = 0;
bool begButtonTimerActive = false;
BegButtonAction begButtonTimerAction = BEG_BUTTON_DISABLED;
uint32_t begButtonStopAtMs = 0;

bool motorMoving(uint8_t index) {
  return motors[index].continuous || motors[index].finiteMove;
}

bool anyMotorMoving() {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    if (motorMoving(i)) return true;
  }
  return false;
}

const char* baseTopicFor(ObjectKind kind) {
  switch (kind) {
    case OBJ_TURNOUT: return TOPIC_TURNOUT;
    case OBJ_SENSOR:  return TOPIC_SENSOR;
    case OBJ_MEMORY:  return TOPIC_MEMORY;
    case OBJ_LIGHT:   return TOPIC_LIGHT;
  }
  return "";
}

const char* kindName(ObjectKind kind) {
  switch (kind) {
    case OBJ_TURNOUT: return "turnout";
    case OBJ_SENSOR:  return "sensor";
    case OBJ_MEMORY:  return "memory";
    case OBJ_LIGHT:   return "light";
  }
  return "unknown";
}

String buildTopic(const char* base, const char* id) {
  return String(settings.jmriChannel) + base + id;
}

void publishObject(uint8_t objectIndex, const char* value, bool retained = true) {
  if (!mqtt.connected()) return;
  String topic = buildTopic(baseTopicFor(objects[objectIndex].kind), objects[objectIndex].fullId);
  mqtt.publish(topic.c_str(), value, retained);
  Serial.printf("PUB %-48s %s\n", topic.c_str(), value);
}

void publishBooleanObject(uint8_t objectIndex, bool state) {
  switch (objects[objectIndex].kind) {
    case OBJ_TURNOUT: publishObject(objectIndex, state ? "THROWN" : "CLOSED"); break;
    case OBJ_SENSOR:  publishObject(objectIndex, state ? "ACTIVE" : "INACTIVE"); break;
    case OBJ_LIGHT:   publishObject(objectIndex, state ? "ON" : "OFF"); break;
    default: break;
  }
}

void publishIntegerObject(uint8_t objectIndex, int32_t value) {
  char valueText[16];
  snprintf(valueText, sizeof(valueText), "%ld", static_cast<long>(value));
  publishObject(objectIndex, valueText);
}

void setBegLed(bool on, bool publish = true) {
  if (begLedState == on && publish) return;
  begLedState = on;
  digitalWrite(PIN_BEG_LED, on ? HIGH : LOW);
  if (publish) publishBooleanObject(O_BEG, on);
}

void refreshIndicatorLeds(bool publishBeg = true) {
  digitalWrite(PIN_MOTOR1_LED, motorMoving(0) ? HIGH : LOW);
  digitalWrite(PIN_MOTOR2_LED, motorMoving(1) ? HIGH : LOW);
  // The global JMRI BEG light is the highest-priority override. When allowed,
  // BEG is the ready/trigger indicator and is lit only while motors are stopped.
  setBegLed(settings.globalBegLedsEnabled && !anyMotorMoving(), publishBeg);
}

void writeShiftRegister(uint8_t value) {
  if (value == shiftByte) return;

  // Disable the outputs while changing the register, then latch atomically.
  digitalWrite(PIN_SHIFT_OE_N, HIGH);
  digitalWrite(PIN_SHIFT_LATCH, LOW);
  shiftOut(PIN_SHIFT_DATA, PIN_SHIFT_CLOCK, MSBFIRST, value);
  digitalWrite(PIN_SHIFT_LATCH, HIGH);
  shiftByte = value;
  digitalWrite(PIN_SHIFT_OE_N, value == 0 ? HIGH : LOW);
}

uint8_t motorNibble(uint8_t index) {
  const MotorState& motor = motors[index];
  if (!motorMoving(index) && (!settings.holdWhenStopped || !motor.everStepped)) return 0;
  return HALF_STEP_SEQUENCE[motor.phase & 0x07];
}

void refreshMotorOutputs() {
  uint8_t output = motorNibble(0) | static_cast<uint8_t>(motorNibble(1) << 4);
  writeShiftRegister(output);
}

void publishMotorState(uint8_t index) {
  const uint8_t base = index == 0 ? O_M1_RUN : O_M2_RUN;
  publishBooleanObject(base + 0, motors[index].continuous);
  publishBooleanObject(base + 1, motors[index].reverse);
  publishBooleanObject(base + 2, motorMoving(index));
  publishIntegerObject(base + 3, motors[index].speed);
  publishIntegerObject(base + 4, 0);
  publishIntegerObject(base + 5, motors[index].position);
}

void saveMotorSettings(uint8_t index) {
  char key[10];
  snprintf(key, sizeof(key), "speed%u", index);
  preferences.putUShort(key, motors[index].speed);
  snprintf(key, sizeof(key), "dir%u", index);
  preferences.putBool(key, motors[index].reverse);

  if (settings.linkMotorGroups && index == 0) {
    motors[1].speed = motors[0].speed;
    motors[1].reverse = motors[0].reverse;
    preferences.putUShort("speed1", motors[1].speed);
    preferences.putBool("dir1", motors[1].reverse);
  }
}

void stopMotor(uint8_t index, bool publish = true) {
  const bool wasMoving = motorMoving(index);
  motors[index].continuous = false;
  motors[index].finiteMove = false;
  motors[index].remainingSteps = 0;
  refreshMotorOutputs();
  refreshIndicatorLeds(publish);
  if (publish && wasMoving) {
    const uint8_t base = index == 0 ? O_M1_RUN : O_M2_RUN;
    publishBooleanObject(base + 0, false);
    publishBooleanObject(base + 2, false);
    publishIntegerObject(base + 4, 0);
    publishIntegerObject(base + 5, motors[index].position);
  }
}

void stopAllMotors(bool publish = true) {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) stopMotor(i, publish);
}

void startContinuous(uint8_t index, bool publish = true) {
  if (motors[index].continuous && !motors[index].finiteMove) return;
  motors[index].finiteMove = false;
  motors[index].remainingSteps = 0;
  motors[index].continuous = true;
  motors[index].nextStepUs = micros();
  refreshIndicatorLeds(publish);
  if (publish) {
    const uint8_t base = index == 0 ? O_M1_RUN : O_M2_RUN;
    publishBooleanObject(base + 0, true);
    publishBooleanObject(base + 2, true);
  }
}

void startFiniteMove(uint8_t index, int32_t signedSteps) {
  if (signedSteps == 0) {
    stopMotor(index);
    return;
  }

  motors[index].continuous = false;
  motors[index].finiteMove = true;
  motors[index].finiteReverse = signedSteps < 0;
  motors[index].remainingSteps = static_cast<uint32_t>(abs(signedSteps));
  motors[index].nextStepUs = micros();
  refreshIndicatorLeds();

  const uint8_t base = index == 0 ? O_M1_RUN : O_M2_RUN;
  publishBooleanObject(base + 0, false);
  publishBooleanObject(base + 2, true);
  // Clear the retained command so it cannot repeat after a restart. An empty
  // payload is ignored by the parser, while an explicit external 0 stops.
  publishObject(base + 4, "");
}

void completeFiniteMove(uint8_t index) {
  motors[index].finiteMove = false;
  refreshMotorOutputs();
  refreshIndicatorLeds();
  const uint8_t base = index == 0 ? O_M1_RUN : O_M2_RUN;
  publishBooleanObject(base + 2, false);
  publishIntegerObject(base + 5, motors[index].position);
}

void serviceMotors() {
  const uint32_t now = micros();
  bool changed = false;

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    MotorState& motor = motors[i];
    if (!motorMoving(i)) continue;

    if (static_cast<int32_t>(now - motor.nextStepUs) < 0) continue;

    const uint32_t intervalUs = 1000000UL / motor.speed;
    motor.nextStepUs += intervalUs;
    // If the loop was delayed, resume from now instead of emitting a burst.
    if (static_cast<int32_t>(now - motor.nextStepUs) >= 0) motor.nextStepUs = now + intervalUs;

    const bool commandedReverse = motor.finiteMove ? motor.finiteReverse : motor.reverse;
    bool reverse = commandedReverse ^ INVERT_MOTOR_DIRECTION[i];
    motor.phase = reverse ? static_cast<uint8_t>((motor.phase + 7) & 0x07)
                          : static_cast<uint8_t>((motor.phase + 1) & 0x07);
    motor.position += commandedReverse ? -1 : 1;
    motor.everStepped = true;
    changed = true;

    if (motor.finiteMove && motor.remainingSteps > 0) {
      --motor.remainingSteps;
      if (motor.remainingSteps == 0) completeFiniteMove(i);
    }
  }

  if (changed) refreshMotorOutputs();
}

void loadSettings() {
  preferences.begin(CONFIG_NAMESPACE, false);

  preferences.getString("wifiSsid", WIFI_SSID).toCharArray(settings.wifiSsid, sizeof(settings.wifiSsid));
  preferences.getString("wifiPass", WIFI_PASSWORD).toCharArray(settings.wifiPassword, sizeof(settings.wifiPassword));
  settings.mqttEnabled = preferences.getBool("mqttEn", true);
  preferences.getString("mqttHost", MQTT_BROKER).toCharArray(settings.mqttBroker, sizeof(settings.mqttBroker));
  settings.mqttPort = preferences.getUShort("mqttPort", MQTT_PORT);
  preferences.getString("mqttUser", MQTT_USER).toCharArray(settings.mqttUser, sizeof(settings.mqttUser));
  preferences.getString("mqttPass", MQTT_PASS).toCharArray(settings.mqttPassword, sizeof(settings.mqttPassword));
  preferences.getString("mqttPref", JMRI_CHANNEL).toCharArray(settings.jmriChannel, sizeof(settings.jmriChannel));
  settings.linkMotorGroups = preferences.getBool("linked", DEFAULT_LINK_MOTOR_GROUPS);
  settings.holdWhenStopped = preferences.getBool("hold", DEFAULT_HOLD_WHEN_STOPPED);
  settings.stopOnConnectionLoss = preferences.getBool("stoploss", DEFAULT_STOP_MOTORS_ON_CONNECTION_LOSS);
  settings.begButtonAction = static_cast<BegButtonAction>(constrain(
      preferences.getUChar("begact", DEFAULT_BEG_BUTTON_ACTION),
      static_cast<uint8_t>(BEG_BUTTON_DISABLED),
      static_cast<uint8_t>(BEG_BUTTON_GROUP_2)));
  settings.begButtonRunIndefinitely = preferences.getBool(
      "begindef", DEFAULT_BEG_BUTTON_RUN_INDEFINITELY);
  settings.begButtonRunSeconds = constrain(
      preferences.getUShort("begtime", DEFAULT_BEG_BUTTON_RUN_SECONDS),
      MIN_SENSOR_RUN_SECONDS, MAX_SENSOR_RUN_SECONDS);
  settings.globalBegLedsEnabled = preferences.getBool("begGlobal", true);
  settings.sensorControlMode = static_cast<SensorControlMode>(constrain(
      preferences.getUChar("senmode", DEFAULT_SENSOR_CONTROL_MODE),
      static_cast<uint8_t>(SENSOR_CONTROL_DISABLED),
      static_cast<uint8_t>(SENSOR_CONTROL_ENTER_EXIT)));
  settings.sensorMotorTarget = static_cast<MotorGroupTarget>(constrain(
      preferences.getUChar("sentarget", DEFAULT_SENSOR_MOTOR_TARGET),
      static_cast<uint8_t>(MOTOR_TARGET_BOTH),
      static_cast<uint8_t>(MOTOR_TARGET_GROUP_2)));
  settings.sensorRunSeconds = constrain(
      preferences.getUShort("sentime", DEFAULT_SENSOR_RUN_SECONDS),
      MIN_SENSOR_RUN_SECONDS, MAX_SENSOR_RUN_SECONDS);
  settings.sensorClearHoldMs = constrain(
      preferences.getUShort("senclear", DEFAULT_SENSOR_CLEAR_HOLD_MS),
      0, MAX_SENSOR_CLEAR_HOLD_MS);
  settings.sensorSamplePeriodMs = constrain(preferences.getUShort("sample", DEFAULT_SENSOR_SAMPLE_PERIOD_MS), 30, 1000);
  settings.occupiedHysteresisMm = constrain(preferences.getUShort("hyst", DEFAULT_OCCUPIED_HYSTERESIS_MM), 0, 500);

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    char key[10];
    snprintf(key, sizeof(key), "speed%u", i);
    motors[i].speed = constrain(preferences.getUShort(key, DEFAULT_SPEED_STEPS_PER_SECOND),
                                MIN_SPEED_STEPS_PER_SECOND, MAX_SPEED_STEPS_PER_SECOND);
    snprintf(key, sizeof(key), "dir%u", i);
    motors[i].reverse = preferences.getBool(key, false);

    snprintf(key, sizeof(key), "thr%u", i);
    ranges[i].thresholdMm = preferences.getUShort(key, DEFAULT_OCCUPIED_THRESHOLD_MM);
  }

  if (settings.linkMotorGroups) {
    motors[1].speed = motors[0].speed;
    motors[1].reverse = motors[0].reverse;
  }
}

void saveAllSettings() {
  preferences.putUInt("cfgver", CONFIG_VERSION);
  preferences.putString("wifiSsid", settings.wifiSsid);
  preferences.putString("wifiPass", settings.wifiPassword);
  preferences.putBool("mqttEn", settings.mqttEnabled);
  preferences.putString("mqttHost", settings.mqttBroker);
  preferences.putUShort("mqttPort", settings.mqttPort);
  preferences.putString("mqttUser", settings.mqttUser);
  preferences.putString("mqttPass", settings.mqttPassword);
  preferences.putString("mqttPref", settings.jmriChannel);
  preferences.putBool("linked", settings.linkMotorGroups);
  preferences.putBool("hold", settings.holdWhenStopped);
  preferences.putBool("stoploss", settings.stopOnConnectionLoss);
  preferences.putUChar("begact", settings.begButtonAction);
  preferences.putBool("begindef", settings.begButtonRunIndefinitely);
  preferences.putUShort("begtime", settings.begButtonRunSeconds);
  preferences.putBool("begGlobal", settings.globalBegLedsEnabled);
  preferences.putUChar("senmode", settings.sensorControlMode);
  preferences.putUChar("sentarget", settings.sensorMotorTarget);
  preferences.putUShort("sentime", settings.sensorRunSeconds);
  preferences.putUShort("senclear", settings.sensorClearHoldMs);
  preferences.putUShort("sample", settings.sensorSamplePeriodMs);
  preferences.putUShort("hyst", settings.occupiedHysteresisMm);

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    char key[10];
    snprintf(key, sizeof(key), "speed%u", i);
    preferences.putUShort(key, motors[i].speed);
    snprintf(key, sizeof(key), "dir%u", i);
    preferences.putBool(key, motors[i].reverse);
    snprintf(key, sizeof(key), "thr%u", i);
    preferences.putUShort(key, ranges[i].thresholdMm);
  }
}

void saveThreshold(uint8_t index) {
  char key[8];
  snprintf(key, sizeof(key), "thr%u", index);
  preferences.putUShort(key, ranges[index].thresholdMm);
}

void buildIdentity() {
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(fullMac, sizeof(fullMac), "%02X:%02X:%02X:%02X:%02X:%02X",
           (uint8_t)mac, (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  snprintf(deviceId, sizeof(deviceId), "MOTORCON_%02X%02X%02X%02X%02X%02X",
           (uint8_t)mac, (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));

  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) {
    snprintf(objects[i].fullId, sizeof(objects[i].fullId), "%s/%s", deviceId, objects[i].localId);
  }
}

void normalizeBrokerHost() {
  const char* start = settings.mqttBroker;
  const char* scheme = strstr(start, "://");
  if (scheme) start = scheme + 3;
  snprintf(mqttHost, sizeof(mqttHost), "%s", start);
  char* slash = strchr(mqttHost, '/');
  if (slash) *slash = '\0';
  char* portSeparator = strchr(mqttHost, ':');
  if (portSeparator) *portSeparator = '\0';
}

void objectStateText(uint8_t objectIndex, char* output, size_t outputSize) {
  const uint8_t motorIndex = objectIndex < O_M2_RUN ? 0 : 1;
  const uint8_t motorBase = motorIndex == 0 ? O_M1_RUN : O_M2_RUN;

  if (objectIndex >= O_M1_RUN && objectIndex <= O_M2_POS) {
    switch (objectIndex - motorBase) {
      case 0: snprintf(output, outputSize, "%s", motors[motorIndex].continuous ? "THROWN" : "CLOSED"); return;
      case 1: snprintf(output, outputSize, "%s", motors[motorIndex].reverse ? "THROWN" : "CLOSED"); return;
      case 2: snprintf(output, outputSize, "%s", motorMoving(motorIndex) ? "ACTIVE" : "INACTIVE"); return;
      case 3: snprintf(output, outputSize, "%u", motors[motorIndex].speed); return;
      case 4: snprintf(output, outputSize, "0"); return;
      case 5: snprintf(output, outputSize, "%ld", static_cast<long>(motors[motorIndex].position)); return;
    }
  }

  if (objectIndex >= O_S1_OCCUPIED && objectIndex <= O_S2_THRESHOLD) {
    const uint8_t sensorIndex = objectIndex < O_S2_OCCUPIED ? 0 : 1;
    const uint8_t sensorBase = sensorIndex == 0 ? O_S1_OCCUPIED : O_S2_OCCUPIED;
    switch (objectIndex - sensorBase) {
      case 0: snprintf(output, outputSize, "%s", ranges[sensorIndex].occupied ? "ACTIVE" : "INACTIVE"); return;
      case 1: snprintf(output, outputSize, "%d", ranges[sensorIndex].distanceMm); return;
      case 2: snprintf(output, outputSize, "%u", ranges[sensorIndex].thresholdMm); return;
    }
  }

  snprintf(output, outputSize, "%s", begLedState ? "ON" : "OFF");
}

void publishAllState() {
  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) {
    char state[20];
    objectStateText(i, state, sizeof(state));
    publishObject(i, state);
  }
}

void publishDiscovery() {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["device"] = deviceId;
  doc["mac"] = fullMac;
  doc["type"] = "motor_controller";
  doc["motorChannels"] = MOTOR_COUNT;
  doc["physicalMotors"] = 4;
  doc["distanceSensors"] = SENSOR_COUNT;
  JsonArray outputs = doc["outputs"].to<JsonArray>();

  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) {
    JsonObject item = outputs.add<JsonObject>();
    item["type"] = kindName(objects[i].kind);
    item["id"] = objects[i].fullId;
    char state[20];
    objectStateText(i, state, sizeof(state));
    item["state"] = state;
    if (objects[i].name[0]) item["name"] = objects[i].name;
  }

  String json;
  serializeJson(doc, json);
  String topic = String(settings.jmriChannel) + DISCOVERY_TOPIC + deviceId;
  mqtt.publish(topic.c_str(), json.c_str(), true);
  Serial.printf("Discovery published: %s\n", topic.c_str());
}

int findObject(const char* id) {
  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) {
    if (strcmp(id, objects[i].fullId) == 0 || strcmp(id, objects[i].localId) == 0) return i;
  }
  return -1;
}

void saveObjectName(uint8_t index) {
  char key[8];
  snprintf(key, sizeof(key), "name%02u", index);
  preferences.putString(key, objects[index].name);
}

void loadObjectNames() {
  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "name%02u", i);
    String name = preferences.getString(key, "");
    snprintf(objects[i].name, sizeof(objects[i].name), "%s", name.c_str());
  }
}

void storeName(const char* id, const char* name) {
  int index = findObject(id);
  if (index < 0 || !name) return;
  snprintf(objects[index].name, sizeof(objects[index].name), "%s", name);
  saveObjectName(index);
}

void requestNames() {
  if (!mqtt.connected()) return;
  JsonDocument doc;
  JsonArray list = doc["outputs"].to<JsonArray>();
  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) list.add(objects[i].fullId);

  String json;
  serializeJson(doc, json);
  String topic = String(settings.jmriChannel) + NAMES_REQ_TOPIC + deviceId;
  mqtt.publish(topic.c_str(), json.c_str());
  lastNameRequestMs = millis();
  Serial.println(F("Name-sync request sent"));
}

void handleNameMessage(const String& topic, const char* message) {
  JsonDocument doc;
  if (deserializeJson(doc, message)) return;

  if (topic.indexOf(NAMES_RESP_TOPIC) >= 0) {
    for (JsonPair pair : doc.as<JsonObject>()) {
      storeName(pair.key().c_str(), pair.value().as<const char*>());
    }
    namesReceived = true;
    publishDiscovery();
    return;
  }

  const char* id = doc["id"];
  const char* name = doc["name"];
  if (id && name) {
    storeName(id, name);
    publishDiscovery();
  }
}

void handleTurnoutCommand(uint8_t objectIndex, const char* message) {
  const bool thrown = strcmp(message, "THROWN") == 0;
  const int8_t motorIndex = objectIndex <= O_M1_DIR ? 0 : (objectIndex >= O_M2_RUN && objectIndex <= O_M2_DIR ? 1 : -1);
  if (motorIndex < 0) return;
  const uint8_t base = motorIndex == 0 ? O_M1_RUN : O_M2_RUN;

  if (objectIndex == base) {
    if (thrown && !motors[motorIndex].continuous) startContinuous(motorIndex);
    else if (!thrown && motors[motorIndex].continuous) stopMotor(motorIndex);
  } else if (objectIndex == base + 1) {
    if (settings.linkMotorGroups && motorIndex == 1) {
      publishBooleanObject(O_M2_DIR, motors[1].reverse);
      return;
    }
    if (motors[motorIndex].reverse == thrown) return;
    motors[motorIndex].reverse = thrown;
    saveMotorSettings(motorIndex);
    publishBooleanObject(base + 1, thrown);
    if (settings.linkMotorGroups && motorIndex == 0) {
      publishBooleanObject(O_M2_DIR, motors[1].reverse);
    }
  }
}

void handleMemoryCommand(uint8_t objectIndex, const char* message) {
  char* end = nullptr;
  long value = strtol(message, &end, 10);
  if (end == message || *end != '\0') return;

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    const uint8_t base = i == 0 ? O_M1_RUN : O_M2_RUN;
    if (objectIndex == base + 3) {
      if (settings.linkMotorGroups && i == 1) {
        publishIntegerObject(O_M2_SPEED, motors[1].speed);
        return;
      }
      const uint16_t newSpeed = constrain(value, MIN_SPEED_STEPS_PER_SECOND, MAX_SPEED_STEPS_PER_SECOND);
      if (motors[i].speed == newSpeed) return;
      motors[i].speed = newSpeed;
      saveMotorSettings(i);
      publishIntegerObject(base + 3, motors[i].speed);
      if (settings.linkMotorGroups && i == 0) publishIntegerObject(O_M2_SPEED, motors[1].speed);
      return;
    }
    if (objectIndex == base + 4) {
      value = constrain(value, -2000000000L, 2000000000L);
      startFiniteMove(i, static_cast<int32_t>(value));
      return;
    }
    if (objectIndex == base + 5) {
      // Position is report-only. Correct a differing inbound write once; ignore
      // the matching publication that comes back to this subscribed client.
      if (value != motors[i].position) publishIntegerObject(base + 5, motors[i].position);
      return;
    }
  }

  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
    const uint8_t base = i == 0 ? O_S1_OCCUPIED : O_S2_OCCUPIED;
    if (objectIndex == base + 1) {
      if (value != ranges[i].distanceMm) publishIntegerObject(base + 1, ranges[i].distanceMm);
      return;
    }
    if (objectIndex == base + 2) {
      const uint16_t newThreshold = constrain(value, 20L, 2000L);
      if (ranges[i].thresholdMm == newThreshold) return;
      ranges[i].thresholdMm = newThreshold;
      saveThreshold(i);
      publishIntegerObject(base + 2, ranges[i].thresholdMm);
      return;
    }
  }
}

void mqttCallback(char* topicText, byte* payload, unsigned int length) {
  if (length >= mqtt.getBufferSize()) return;
  String message;
  if (!message.reserve(length + 1)) return;
  for (unsigned int i = 0; i < length; ++i) message += static_cast<char>(payload[i]);
  message.trim();
  String topic(topicText);
  String otaMessage(message);
  otaMessage.toUpperCase();
  if (DeviceOta::handleMqtt(mqtt, settings.mqttEnabled, !anyMotorMoving(), topic, otaMessage)) return;

  // JMRI's global BEG light controls every device. OFF always wins; ON only
  // permits the local ready behavior (motors must still be stopped).
  const String globalBegTopic = buildTopic(TOPIC_LIGHT, "BEG");
  if (topic == globalBegTopic) {
    bool newEnabled;
    if (message == "ON") newEnabled = true;
    else if (message == "OFF") newEnabled = false;
    else return;

    if (settings.globalBegLedsEnabled != newEnabled) {
      settings.globalBegLedsEnabled = newEnabled;
      preferences.putBool("begGlobal", newEnabled);
      refreshIndicatorLeds();
      Serial.printf("Global BEG LEDs: %s\n", newEnabled ? "ENABLED" : "DISABLED");
    }
    return;
  }

  if (topic.indexOf(deviceId) < 0) return;
  if (topic.indexOf(NAMES_RESP_TOPIC) >= 0 || topic.indexOf(NAMES_PUSH_TOPIC) >= 0) {
    handleNameMessage(topic, message.c_str());
    return;
  }

  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) {
    String expected = buildTopic(baseTopicFor(objects[i].kind), objects[i].fullId);
    if (topic != expected) continue;
    if (objects[i].kind == OBJ_TURNOUT) handleTurnoutCommand(i, message.c_str());
    else if (objects[i].kind == OBJ_MEMORY) handleMemoryCommand(i, message.c_str());
    return;
  }
}

void initializeRangeSensors() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  const uint8_t xshutPins[SENSOR_COUNT] = { PIN_XSHUT_1, PIN_XSHUT_2 };
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
    pinMode(xshutPins[i], OUTPUT);
    digitalWrite(xshutPins[i], LOW);
  }
  delay(10);

  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
    // INPUT releases XSHUT; the breakout's pull-up raises it safely.
    pinMode(xshutPins[i], INPUT);
    delay(10);
    ranges[i].present = rangeSensors[i].begin(SENSOR_I2C_ADDRESS[i], false, &Wire);
    if (ranges[i].present) {
      rangeSensors[i].startRangeContinuous(settings.sensorSamplePeriodMs);
      Serial.printf("VL53L0X %u ready at 0x%02X\n", i + 1, SENSOR_I2C_ADDRESS[i]);
    } else {
      Serial.printf("VL53L0X %u not detected\n", i + 1);
      pinMode(xshutPins[i], OUTPUT);
      digitalWrite(xshutPins[i], LOW);
    }
  }
}

void startSensorControlledMotors() {
  switch (settings.sensorMotorTarget) {
    case MOTOR_TARGET_GROUP_1:
      startContinuous(0);
      break;
    case MOTOR_TARGET_GROUP_2:
      startContinuous(1);
      break;
    case MOTOR_TARGET_BOTH:
    default:
      startContinuous(0);
      startContinuous(1);
      break;
  }
  sensorAutomationRunning = true;
  sensorAutomationExit = -1;
  sensorAutomationExitClearStartMs = 0;
  sensorAutomationStartedMs = millis();
  sensorAutomationStopAtMs = millis() + static_cast<uint32_t>(settings.sensorRunSeconds) * 1000UL;
}

void stopSensorControlledMotors() {
  switch (settings.sensorMotorTarget) {
    case MOTOR_TARGET_GROUP_1:
      stopMotor(0);
      break;
    case MOTOR_TARGET_GROUP_2:
      stopMotor(1);
      break;
    case MOTOR_TARGET_BOTH:
    default:
      stopAllMotors();
      break;
  }
  sensorAutomationRunning = false;
  sensorAutomationEntry = -1;
  sensorAutomationExit = -1;
  sensorAutomationStartedMs = 0;
  sensorAutomationStopAtMs = 0;
  sensorAutomationExitClearStartMs = 0;
}

bool sensorAutomationHardwareAvailable() {
  if (settings.sensorControlMode == SENSOR_CONTROL_ANY_TIMED) {
    return ranges[0].present || ranges[1].present;
  }
  if (settings.sensorControlMode == SENSOR_CONTROL_ENTER_EXIT) {
    return ranges[0].present && ranges[1].present;
  }
  return false;
}

void handleSensorActivated(uint8_t sensorIndex) {
  if (!sensorAutomationHardwareAvailable()) return;
  switch (settings.sensorControlMode) {
    case SENSOR_CONTROL_ANY_TIMED:
      startSensorControlledMotors();
      // Every new detection restarts the configured time window.
      sensorAutomationStopAtMs = millis() + static_cast<uint32_t>(settings.sensorRunSeconds) * 1000UL;
      break;

    case SENSOR_CONTROL_ENTER_EXIT:
      if (!sensorAutomationRunning) {
        sensorAutomationEntry = sensorIndex;
        startSensorControlledMotors();
      } else if (sensorAutomationEntry != sensorIndex &&
                 millis() - sensorAutomationStartedMs >= ENTER_EXIT_GUARD_MS) {
        sensorAutomationExit = sensorIndex;
        sensorAutomationExitClearStartMs = 0;
      }
      break;

    case SENSOR_CONTROL_DISABLED:
    default:
      break;
  }
}

void serviceSensorAutomation() {
  if (!sensorAutomationRunning) return;
  if (settings.sensorControlMode == SENSOR_CONTROL_ENTER_EXIT && sensorAutomationExit >= 0) {
    if (!ranges[sensorAutomationExit].occupied) {
      if (settings.sensorClearHoldMs == 0) {
        stopSensorControlledMotors();
        return;
      }
      if (sensorAutomationExitClearStartMs == 0) {
        sensorAutomationExitClearStartMs = millis();
      } else if (millis() - sensorAutomationExitClearStartMs >= settings.sensorClearHoldMs) {
        stopSensorControlledMotors();
        return;
      }
    } else {
      sensorAutomationExitClearStartMs = 0;
    }
  }
  if (static_cast<int32_t>(millis() - sensorAutomationStopAtMs) < 0) return;

  // This is the normal stop for time-based mode and a fail-safe stop for an
  // enter-exit sequence where the second sensor was never reached.
  stopSensorControlledMotors();
}

void serviceRangeSensors() {
  for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
    if (!ranges[i].present || !rangeSensors[i].isRangeComplete()) continue;

    const uint8_t status = rangeSensors[i].readRangeStatus();
    const int16_t newDistance = status == 0 ? static_cast<int16_t>(rangeSensors[i].readRangeResult()) : -1;
    const bool previousOccupied = ranges[i].occupied;

    if (newDistance < 0) {
      ranges[i].occupied = false;
    } else if (!ranges[i].occupied && newDistance <= ranges[i].thresholdMm) {
      ranges[i].occupied = true;
    } else if (ranges[i].occupied && newDistance >= ranges[i].thresholdMm + settings.occupiedHysteresisMm) {
      ranges[i].occupied = false;
    }
    ranges[i].distanceMm = newDistance;

    const uint8_t base = i == 0 ? O_S1_OCCUPIED : O_S2_OCCUPIED;
    if (ranges[i].occupied != previousOccupied) {
      publishBooleanObject(base, ranges[i].occupied);
      if (ranges[i].occupied) handleSensorActivated(i);
    }

    const bool changedEnough = abs(ranges[i].distanceMm - ranges[i].lastPublishedMm) >= SENSOR_MIN_PUBLISH_CHANGE_MM;
    const bool heartbeatDue = millis() - ranges[i].lastPublishMs >= SENSOR_HEARTBEAT_MS;
    if (changedEnough || heartbeatDue) {
      publishIntegerObject(base + 1, ranges[i].distanceMm);
      ranges[i].lastPublishedMm = ranges[i].distanceMm;
      ranges[i].lastPublishMs = millis();
    }
  }
}

void handleButton() {
  const bool reading = digitalRead(PIN_BEG_BUTTON);
  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    lastButtonChangeMs = millis();
  }

  if (millis() - lastButtonChangeMs < BUTTON_DEBOUNCE_MS || reading == stableButtonState) return;
  stableButtonState = reading;
  if (stableButtonState != LOW) return;

  switch (settings.begButtonAction) {
    case BEG_BUTTON_BOTH_GROUPS:
      if (anyMotorMoving()) {
        stopAllMotors();
        begButtonTimerActive = false;
      }
      else {
        for (uint8_t i = 0; i < MOTOR_COUNT; ++i) startContinuous(i);
        begButtonTimerAction = settings.begButtonAction;
        begButtonTimerActive = !settings.begButtonRunIndefinitely;
        begButtonStopAtMs = millis() + static_cast<uint32_t>(settings.begButtonRunSeconds) * 1000UL;
      }
      break;

    case BEG_BUTTON_GROUP_1:
      if (motorMoving(0)) {
        stopMotor(0);
        begButtonTimerActive = false;
      } else {
        startContinuous(0);
        begButtonTimerAction = settings.begButtonAction;
        begButtonTimerActive = !settings.begButtonRunIndefinitely;
        begButtonStopAtMs = millis() + static_cast<uint32_t>(settings.begButtonRunSeconds) * 1000UL;
      }
      break;

    case BEG_BUTTON_GROUP_2:
      if (motorMoving(1)) {
        stopMotor(1);
        begButtonTimerActive = false;
      } else {
        startContinuous(1);
        begButtonTimerAction = settings.begButtonAction;
        begButtonTimerActive = !settings.begButtonRunIndefinitely;
        begButtonStopAtMs = millis() + static_cast<uint32_t>(settings.begButtonRunSeconds) * 1000UL;
      }
      break;

    case BEG_BUTTON_DISABLED:
    default:
      break;
  }
}

bool serviceSetupButton() {
  const bool pressed = digitalRead(PIN_SETUP_BUTTON) == LOW;
  if (isConfigAccessPointActive()) {
    setupButtonDown = false;
    return pressed;
  }
  if (!pressed) {
    setupButtonDown = false;
    return false;
  }

  if (!setupButtonDown) {
    setupButtonDown = true;
    setupButtonHoldStartMs = millis();
    stopAllMotors(false);
  } else if (millis() - setupButtonHoldStartMs >= SETUP_BUTTON_HOLD_MS) {
    setupButtonDown = false;
    Serial.println(F("BOOT held for 3 seconds: entering setup portal"));
    startConfigAccessPoint();
  }
  return true;
}

void serviceBegButtonTimer() {
  if (!begButtonTimerActive || static_cast<int32_t>(millis() - begButtonStopAtMs) < 0) return;

  switch (begButtonTimerAction) {
    case BEG_BUTTON_GROUP_1:
      stopMotor(0);
      break;
    case BEG_BUTTON_GROUP_2:
      stopMotor(1);
      break;
    case BEG_BUTTON_BOTH_GROUPS:
      stopAllMotors();
      break;
    case BEG_BUTTON_DISABLED:
    default:
      break;
  }
  begButtonTimerActive = false;
  begButtonTimerAction = BEG_BUTTON_DISABLED;
}

void serviceWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWiFiRetryMs < WIFI_RETRY_MS) return;
  lastWiFiRetryMs = millis();
  Serial.println(F("WiFi connecting..."));
  WiFi.begin(settings.wifiSsid, settings.wifiPassword);
}

void onMqttConnected() {
  Serial.println(F("MQTT connected"));
  refreshIndicatorLeds(false);

  // Publish safe current state first so stale retained RUN commands cannot start motors.
  publishAllState();

  String subscription = String(settings.jmriChannel) + "#";
  mqtt.subscribe(subscription.c_str());
  Serial.printf("Subscribed: %s\n", subscription.c_str());

  publishDiscovery();
  namesReceived = false;
  requestNames();
}

void serviceMqtt() {
  if (!settings.mqttEnabled || settings.mqttBroker[0] == '\0') {
    if (mqtt.connected()) mqtt.disconnect();
    mqttWasConnected = false;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;

  if (mqtt.connected()) {
    mqttWasConnected = true;
    mqtt.loop();
    if (!namesReceived && millis() - lastNameRequestMs >= NAME_RETRY_MS) requestNames();
    return;
  }

  if (mqttWasConnected) {
    mqttWasConnected = false;
    if (settings.stopOnConnectionLoss) stopAllMotors(false);
    else refreshIndicatorLeds(false);
  }

  // A connection attempt can briefly block. Do not attempt one during local motion.
  if (anyMotorMoving() || millis() - lastMqttRetryMs < MQTT_RETRY_MS) return;
  lastMqttRetryMs = millis();

  String clientId(deviceId);
  bool connected = strlen(settings.mqttUser) > 0
                     ? mqtt.connect(clientId.c_str(), settings.mqttUser, settings.mqttPassword)
                     : mqtt.connect(clientId.c_str());
  if (connected) onMqttConnected();
  else Serial.printf("MQTT connect failed, rc=%d\n", mqtt.state());
}

void setupHardware() {
  pinMode(PIN_SHIFT_OE_N, OUTPUT);
  digitalWrite(PIN_SHIFT_OE_N, HIGH);
  pinMode(PIN_SHIFT_DATA, OUTPUT);
  pinMode(PIN_SHIFT_CLOCK, OUTPUT);
  pinMode(PIN_SHIFT_LATCH, OUTPUT);
  digitalWrite(PIN_SHIFT_LATCH, LOW);
  shiftOut(PIN_SHIFT_DATA, PIN_SHIFT_CLOCK, MSBFIRST, 0);
  digitalWrite(PIN_SHIFT_LATCH, HIGH);
  shiftByte = 0;

  pinMode(PIN_MOTOR1_LED, OUTPUT);
  pinMode(PIN_MOTOR2_LED, OUTPUT);
  pinMode(PIN_BEG_LED, OUTPUT);
  pinMode(PIN_BEG_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SETUP_BUTTON, INPUT_PULLUP);
  refreshIndicatorLeds(false);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\nMQTT Motor Controller starting"));

  setupHardware();
  buildIdentity();
  loadSettings();
  loadObjectNames();
  normalizeBrokerHost();
  DeviceOta::begin(settings.jmriChannel, deviceId);
  initializeRangeSensors();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  initializeConfigPortal();
  WiFi.begin(settings.wifiSsid, settings.wifiPassword);
  lastWiFiRetryMs = millis();

  mqtt.setServer(mqttHost, settings.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(4096);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(2);

  Serial.printf("Device: %s (%s)\n", deviceId, fullMac);
  Serial.println(F("Motor outputs safely disabled"));
}

void loop() {
  serviceMotors();
  handleButton();
  serviceBegButtonTimer();
  const bool setupButtonHeld = serviceSetupButton();
  if (!setupButtonHeld) {
    serviceRangeSensors();
    serviceSensorAutomation();
  }
  serviceConfigPortal();
  DeviceSerialSetup::service(settings.wifiSsid, sizeof(settings.wifiSsid),
                             settings.wifiPassword, sizeof(settings.wifiPassword),
                             saveAllSettings, startConfigAccessPoint);
  serviceWiFi();
  serviceMqtt();
  DeviceOta::service(mqtt, settings.mqttEnabled, !anyMotorMoving());
  serviceMotors();
  delay(0);
}
