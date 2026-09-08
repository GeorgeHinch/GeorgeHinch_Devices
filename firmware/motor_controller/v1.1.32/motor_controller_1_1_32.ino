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

#define FIRMWARE_VERSION "1.1.32"
const char CONFIG_NAMESPACE[] = "device_cfg";
const uint32_t CONFIG_VERSION = 1;
constexpr uint16_t OUTPUT_TEST_BLINK_MS = 350;
constexpr uint32_t OUTPUT_TEST_TIMEOUT_MS = 30000;

#include "DeviceOta.h"
#include "DeviceBootButton.h"
#include "DeviceMqttEnrollment.h"
#include "DeviceWifiProvisioning.h"
#include "DeviceSerialSetup.h"

WiFiClient networkClient;
PubSubClient mqtt(networkClient);
Preferences preferences;
Adafruit_VL53L0X rangeSensors[SENSOR_COUNT];

char deviceId[24];
char fullMac[18];
char mqttHost[128];
String stationStateTopic;
String stationCommandTopic;
const String stationBroadcastTopic = "hakomachi/broadcast/command";
const String stationGroupTopic = "hakomachi/groups/motor-controller/command";
bool mqttEnrollmentAttempted = false;

struct DeviceSettings {
  char wifiSsid[33];
  char wifiPassword[65];
  bool mqttEnabled = true;
  char mqttBroker[128];
  uint16_t mqttPort = 1883;
  char mqttUser[65];
  char mqttPassword[65];
  char mqttTopicRoot[96];
  char jmriChannel[96];
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
  uint8_t motorLedAssignment[MOTOR_COUNT] = {OUTPUT_RESERVED, OUTPUT_RESERVED};
};

DeviceSettings settings;

struct MotorState {
  bool continuous = false;
  bool finiteMove = false;
  bool reverse = false;
  // Disabled groups are omitted from automatic sensor-trigger actions. Direct
  // JMRI and external-button targets still run the group in its saved direction.
  bool enabled = true;
  bool finiteReverse = false;
  bool everStepped = false;
  uint8_t phase = 0;
  uint16_t speed = DEFAULT_SPEED_STEPS_PER_SECOND;
  uint32_t nextStepUs = 0;
  uint32_t remainingSteps = 0;
  int32_t position = 0;
  bool positionedArc = false;
  bool activeEndpointIsEnd = true;
  bool positionKnown = false;
  int32_t arcStart = 0;
  int32_t arcEnd = 512;
};

struct RangeState {
  bool present = false;
  bool occupied = false;
  int16_t distanceMm = -1;
  int16_t lastPublishedMm = -32768;
  uint16_t thresholdMm = DEFAULT_OCCUPIED_THRESHOLD_MM;
  uint16_t baselineMm = 0;
  bool calibrated = false;
  bool calibrating = false;
  uint8_t calibrationSampleCount = 0;
  uint16_t calibrationSamples[SENSOR_CALIBRATION_SAMPLE_COUNT] = {};
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
  O_M1_LED, O_M2_LED,
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
  {OBJ_LIGHT, "BEG", {}, {}},
  {OBJ_LIGHT, "M1_LED", {}, {}}, {OBJ_LIGHT, "M2_LED", {}, {}}
};

uint8_t shiftByte = 0;
bool begLedState = false;
bool jmriMotorLedState[MOTOR_COUNT] = {};
int8_t activeMotorLedTest = -1;
bool motorLedTestPhase = false;
uint32_t motorLedTestStartedMs = 0;
uint32_t motorLedTestLastToggleMs = 0;
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
DeviceBootButton::HoldProtection setupButton(PIN_SETUP_BUTTON);
bool mqttWasConnected = false;
bool namesReceived = false;
uint32_t lastButtonChangeMs = 0;
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

bool motorAtActiveEndpoint(uint8_t index) {
  const MotorState& motor = motors[index];
  if (!motor.positionedArc || !motor.positionKnown) return false;
  return motor.position == (motor.activeEndpointIsEnd ? motor.arcEnd : motor.arcStart);
}

bool motorRunState(uint8_t index) {
  return motors[index].positionedArc ? motorAtActiveEndpoint(index) : motors[index].continuous;
}

const char* motorModeText(uint8_t index) {
  if (!motors[index].enabled) return "disabled";
  if (motors[index].positionedArc) return "arc";
  return motors[index].reverse ? "ccw" : "cw";
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

bool motorLedIsJmriAvailable(uint8_t index) {
  return index < MOTOR_COUNT && settings.motorLedAssignment[index] != OUTPUT_RESERVED;
}

bool effectiveMotorLedState(uint8_t index) {
  if (index >= MOTOR_COUNT) return false;
  if (activeMotorLedTest == static_cast<int8_t>(index)) return motorLedTestPhase;
  const OutputAssignment assignment = static_cast<OutputAssignment>(settings.motorLedAssignment[index]);
  const bool localState = motorMoving(index);
  if (assignment == OUTPUT_RESERVED) return localState;
  if (assignment == OUTPUT_JMRI) return jmriMotorLedState[index];
  return localState || jmriMotorLedState[index];
}

bool objectJmriEnabled(uint8_t objectIndex) {
  if (objectIndex == O_M1_LED) return motorLedIsJmriAvailable(0);
  if (objectIndex == O_M2_LED) return motorLedIsJmriAvailable(1);
  return true;
}

void setMotorLed(uint8_t index) {
  const uint8_t pin = index == 0 ? PIN_MOTOR1_LED : PIN_MOTOR2_LED;
  digitalWrite(pin, effectiveMotorLedState(index) ? HIGH : LOW);
}

void refreshIndicatorLeds(bool publishBeg = true) {
  setMotorLed(0);
  setMotorLed(1);
  // The global JMRI BEG light is the highest-priority override. When allowed,
  // BEG is the ready/trigger indicator and is lit only while motors are stopped.
  setBegLed(settings.globalBegLedsEnabled && !anyMotorMoving(), publishBeg);
}

void setMotorLedTest(uint8_t index, bool enabled) {
  if (index >= MOTOR_COUNT) return;
  if (enabled) {
    activeMotorLedTest = static_cast<int8_t>(index);
    motorLedTestPhase = true;
    motorLedTestStartedMs = millis();
    motorLedTestLastToggleMs = motorLedTestStartedMs;
  } else if (activeMotorLedTest == static_cast<int8_t>(index)) {
    activeMotorLedTest = -1;
    motorLedTestPhase = false;
  }
  refreshIndicatorLeds(false);
}

bool serviceMotorLedTest(uint32_t now) {
  if (activeMotorLedTest < 0) return false;
  if ((now - motorLedTestStartedMs) >= OUTPUT_TEST_TIMEOUT_MS) {
    activeMotorLedTest = -1;
    motorLedTestPhase = false;
    refreshIndicatorLeds(false);
    return true;
  }
  if ((now - motorLedTestLastToggleMs) >= OUTPUT_TEST_BLINK_MS) {
    motorLedTestLastToggleMs = now;
    motorLedTestPhase = !motorLedTestPhase;
    refreshIndicatorLeds(false);
  }
  return false;
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
  publishBooleanObject(base + 0, motorRunState(index));
  publishBooleanObject(base + 1, motors[index].reverse);
  publishBooleanObject(base + 2, motorMoving(index));
  publishIntegerObject(base + 3, motors[index].speed);
  publishIntegerObject(base + 4, 0);
  publishIntegerObject(base + 5, motors[index].position);
}

void saveMotorSettings(uint8_t index) {
  char key[12];
  snprintf(key, sizeof(key), "speed%u", index);
  preferences.putUShort(key, motors[index].speed);
  snprintf(key, sizeof(key), "dir%u", index);
  preferences.putBool(key, motors[index].reverse);
  snprintf(key, sizeof(key), "enabled%u", index);
  preferences.putBool(key, motors[index].enabled);
  snprintf(key, sizeof(key), "arc%u", index);
  preferences.putBool(key, motors[index].positionedArc);
  snprintf(key, sizeof(key), "actEnd%u", index);
  preferences.putBool(key, motors[index].activeEndpointIsEnd);
  snprintf(key, sizeof(key), "arcStart%u", index);
  preferences.putInt(key, motors[index].arcStart);
  snprintf(key, sizeof(key), "arcEnd%u", index);
  preferences.putInt(key, motors[index].arcEnd);
  snprintf(key, sizeof(key), "pos%u", index);
  preferences.putInt(key, motors[index].position);
  snprintf(key, sizeof(key), "posKnown%u", index);
  preferences.putBool(key, motors[index].positionKnown);

  if (settings.linkMotorGroups && index == 0) {
    motors[1].speed = motors[0].speed;
    motors[1].reverse = motors[0].reverse;
    motors[1].enabled = motors[0].enabled;
    motors[1].positionedArc = motors[0].positionedArc;
    motors[1].activeEndpointIsEnd = motors[0].activeEndpointIsEnd;
    motors[1].arcStart = motors[0].arcStart;
    motors[1].arcEnd = motors[0].arcEnd;
    preferences.putUShort("speed1", motors[1].speed);
    preferences.putBool("dir1", motors[1].reverse);
    preferences.putBool("enabled1", motors[1].enabled);
    preferences.putBool("arc1", motors[1].positionedArc);
    preferences.putBool("actEnd1", motors[1].activeEndpointIsEnd);
    preferences.putInt("arcStart1", motors[1].arcStart);
    preferences.putInt("arcEnd1", motors[1].arcEnd);
  }
}

void stopMotor(uint8_t index, bool publish = true) {
  const bool wasMoving = motorMoving(index);
  const bool wasFinite = motors[index].finiteMove;
  motors[index].continuous = false;
  motors[index].finiteMove = false;
  motors[index].remainingSteps = 0;
  if (wasFinite) {
    char key[12];
    motors[index].positionKnown = true;
    snprintf(key, sizeof(key), "pos%u", index);
    preferences.putInt(key, motors[index].position);
    snprintf(key, sizeof(key), "posKnown%u", index);
    preferences.putBool(key, true);
    snprintf(key, sizeof(key), "moving%u", index);
    preferences.putBool(key, false);
  }
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

bool moveMotorToEndpoint(uint8_t index, bool endEndpoint);
void setConfiguredRun(uint8_t index, bool active, bool publish = true);
void publishStationState();

void startConfiguredContinuous(uint8_t index, bool publish = true) {
  if (!motors[index].enabled) return;
  setConfiguredRun(index, true, publish);
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
  char movingKey[10];
  snprintf(movingKey, sizeof(movingKey), "moving%u", index);
  preferences.putBool(movingKey, true);
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
  publishBooleanObject(base + 0, motorRunState(index));
  publishBooleanObject(base + 2, false);
  publishIntegerObject(base + 5, motors[index].position);
  motors[index].positionKnown = true;
  char key[12];
  snprintf(key, sizeof(key), "pos%u", index);
  preferences.putInt(key, motors[index].position);
  snprintf(key, sizeof(key), "posKnown%u", index);
  preferences.putBool(key, true);
  snprintf(key, sizeof(key), "moving%u", index);
  preferences.putBool(key, false);
  publishStationState();
}

bool moveMotorToPosition(uint8_t index, int32_t target) {
  if (!motors[index].positionKnown) return false;
  startFiniteMove(index, target - motors[index].position);
  return true;
}

bool moveMotorToEndpoint(uint8_t index, bool endEndpoint) {
  if (!motors[index].positionedArc) return false;
  return moveMotorToPosition(index, endEndpoint ? motors[index].arcEnd : motors[index].arcStart);
}

void setConfiguredRun(uint8_t index, bool active, bool publish) {
  MotorState& motor = motors[index];
  if (motor.positionedArc) {
    moveMotorToEndpoint(index, active ? motor.activeEndpointIsEnd : !motor.activeEndpointIsEnd);
    return;
  }
  if (active) startContinuous(index, publish); else stopMotor(index, publish);
}

void markMotorEndpoint(uint8_t index, bool endEndpoint) {
  stopMotor(index, false);
  MotorState& motor = motors[index];
  if (!motor.positionKnown) {
    motor.position = 0;
    motor.positionKnown = true;
  }
  if (endEndpoint) motor.arcEnd = motor.position;
  else motor.arcStart = motor.position;
  saveMotorSettings(index);
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
  preferences.getString("mqttRoot", "").toCharArray(settings.mqttTopicRoot, sizeof(settings.mqttTopicRoot));
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
    char key[12];
    snprintf(key, sizeof(key), "led%u", i);
    settings.motorLedAssignment[i] = constrain(
        preferences.getUChar(key, settings.motorLedAssignment[i]), OUTPUT_RESERVED, OUTPUT_SHARED);
  }

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    char key[10];
    snprintf(key, sizeof(key), "speed%u", i);
    motors[i].speed = constrain(preferences.getUShort(key, DEFAULT_SPEED_STEPS_PER_SECOND),
                                MIN_SPEED_STEPS_PER_SECOND, MAX_SPEED_STEPS_PER_SECOND);
    snprintf(key, sizeof(key), "dir%u", i);
    motors[i].reverse = preferences.getBool(key, false);
    snprintf(key, sizeof(key), "enabled%u", i);
    motors[i].enabled = preferences.getBool(key, true);
    snprintf(key, sizeof(key), "arc%u", i);
    motors[i].positionedArc = preferences.getBool(key, false);
    snprintf(key, sizeof(key), "actEnd%u", i);
    motors[i].activeEndpointIsEnd = preferences.getBool(key, true);
    snprintf(key, sizeof(key), "arcStart%u", i);
    motors[i].arcStart = preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "arcEnd%u", i);
    motors[i].arcEnd = preferences.getInt(key, 512);
    snprintf(key, sizeof(key), "pos%u", i);
    motors[i].position = preferences.getInt(key, 0);
    snprintf(key, sizeof(key), "posKnown%u", i);
    motors[i].positionKnown = preferences.getBool(key, false);
    snprintf(key, sizeof(key), "moving%u", i);
    if (preferences.getBool(key, false)) motors[i].positionKnown = false;
    preferences.putBool(key, false);

    snprintf(key, sizeof(key), "thr%u", i);
    ranges[i].thresholdMm = preferences.getUShort(key, DEFAULT_OCCUPIED_THRESHOLD_MM);
    snprintf(key, sizeof(key), "base%u", i);
    ranges[i].baselineMm = preferences.getUShort(key, 0);
    snprintf(key, sizeof(key), "cal%u", i);
    ranges[i].calibrated = preferences.getBool(key, false);
  }

  if (settings.linkMotorGroups) {
    motors[1].speed = motors[0].speed;
    motors[1].reverse = motors[0].reverse;
    motors[1].enabled = motors[0].enabled;
    motors[1].positionedArc = motors[0].positionedArc;
    motors[1].activeEndpointIsEnd = motors[0].activeEndpointIsEnd;
    motors[1].arcStart = motors[0].arcStart;
    motors[1].arcEnd = motors[0].arcEnd;
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
  preferences.putString("mqttRoot", settings.mqttTopicRoot);
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
    char key[12];
    snprintf(key, sizeof(key), "led%u", i);
    preferences.putUChar(key, settings.motorLedAssignment[i]);
  }

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    char key[10];
    snprintf(key, sizeof(key), "speed%u", i);
    preferences.putUShort(key, motors[i].speed);
    snprintf(key, sizeof(key), "dir%u", i);
    preferences.putBool(key, motors[i].reverse);
    snprintf(key, sizeof(key), "enabled%u", i);
    preferences.putBool(key, motors[i].enabled);
    snprintf(key, sizeof(key), "arc%u", i);
    preferences.putBool(key, motors[i].positionedArc);
    snprintf(key, sizeof(key), "actEnd%u", i);
    preferences.putBool(key, motors[i].activeEndpointIsEnd);
    snprintf(key, sizeof(key), "arcStart%u", i);
    preferences.putInt(key, motors[i].arcStart);
    snprintf(key, sizeof(key), "arcEnd%u", i);
    preferences.putInt(key, motors[i].arcEnd);
    snprintf(key, sizeof(key), "pos%u", i);
    preferences.putInt(key, motors[i].position);
    snprintf(key, sizeof(key), "posKnown%u", i);
    preferences.putBool(key, motors[i].positionKnown);
    snprintf(key, sizeof(key), "thr%u", i);
    preferences.putUShort(key, ranges[i].thresholdMm);
    snprintf(key, sizeof(key), "base%u", i);
    preferences.putUShort(key, ranges[i].baselineMm);
    snprintf(key, sizeof(key), "cal%u", i);
    preferences.putBool(key, ranges[i].calibrated);
  }
}

void saveThreshold(uint8_t index) {
  char key[8];
  snprintf(key, sizeof(key), "thr%u", index);
  preferences.putUShort(key, ranges[index].thresholdMm);
  snprintf(key, sizeof(key), "base%u", index);
  preferences.putUShort(key, ranges[index].baselineMm);
  snprintf(key, sizeof(key), "cal%u", index);
  preferences.putBool(key, ranges[index].calibrated);
}

void serviceMqttEnrollment() {
  if (mqttEnrollmentAttempted || WiFi.status() != WL_CONNECTED ||
      DeviceMqttEnrollment::isManaged(settings.mqttUser, settings.mqttTopicRoot)) return;
  mqttEnrollmentAttempted = true;
  const char* capabilities[] = {"motor1Run", "motor1Direction", "motor1Speed", "motor2Run", "motor2Direction", "motor2Speed", "sensor1", "sensor2"};
  const bool setupHandoff = DeviceWifiProvisioning::isSetupNetwork();
  if (!DeviceMqttEnrollment::enroll(
          deviceId, "Motor Controller", DEVICE_TYPE, "Motor Controller", FIRMWARE_VERSION,
          capabilities, sizeof(capabilities) / sizeof(capabilities[0]),
          settings.wifiSsid, sizeof(settings.wifiSsid),
          settings.wifiPassword, sizeof(settings.wifiPassword),
          settings.mqttBroker, sizeof(settings.mqttBroker), settings.mqttPort,
          settings.mqttUser, sizeof(settings.mqttUser), settings.mqttPassword, sizeof(settings.mqttPassword),
          settings.mqttTopicRoot, sizeof(settings.mqttTopicRoot))) return;
  settings.mqttEnabled = true;
  strlcpy(settings.jmriChannel, "hakomachi/jmri/", sizeof(settings.jmriChannel));
  saveAllSettings();
  if (setupHandoff) {
    Serial.println(F("Zero-config enrollment saved; restarting on the production network."));
    delay(250);
    ESP.restart();
  }
  normalizeBrokerHost();
  stationStateTopic = String(settings.mqttTopicRoot) + "/state";
  stationCommandTopic = String(settings.mqttTopicRoot) + "/command";
  mqtt.setServer(mqttHost, settings.mqttPort);
  DeviceOta::begin(settings.jmriChannel, deviceId);
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
      case 0: snprintf(output, outputSize, "%s", motorRunState(motorIndex) ? "THROWN" : "CLOSED"); return;
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

  if (objectIndex == O_M1_LED || objectIndex == O_M2_LED) {
    const uint8_t index = objectIndex == O_M1_LED ? 0 : 1;
    snprintf(output, outputSize, "%s", effectiveMotorLedState(index) ? "ON" : "OFF");
    return;
  }

  snprintf(output, outputSize, "%s", begLedState ? "ON" : "OFF");
}

void publishAllState() {
  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) {
    if (!objectJmriEnabled(i)) continue;
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
    if (objects[i].kind == OBJ_MEMORY || !objectJmriEnabled(i)) continue;
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
  for (uint8_t i = 0; i < OBJECT_COUNT; ++i) {
    if (objects[i].kind != OBJ_MEMORY && objectJmriEnabled(i)) list.add(objects[i].fullId);
  }

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
    JsonObject names = doc["names"].as<JsonObject>();
    if (names.isNull()) return;
    for (JsonPair pair : names) {
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
    setConfiguredRun(motorIndex, thrown);
  } else if (objectIndex == base + 1) {
    if (settings.linkMotorGroups && motorIndex == 1) {
      // We subscribe to the same JMRI state topics that we publish. Only
      // correct a genuinely different linked value; otherwise our own retained
      // publication would be echoed back and republished forever.
      if (motors[1].reverse != thrown) {
        publishBooleanObject(O_M2_DIR, motors[1].reverse);
      }
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
        if (value != motors[1].speed) {
          publishIntegerObject(O_M2_SPEED, motors[1].speed);
        }
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
      ranges[i].baselineMm = 0;
      ranges[i].calibrated = true;
      saveThreshold(i);
      publishIntegerObject(base + 2, ranges[i].thresholdMm);
      return;
    }
  }
}

void appendStationConfiguration(JsonDocument& doc) {
  JsonObject configuration = doc["configuration"].to<JsonObject>();
  configuration["contract"] = "hakomachi.device-ui/v1";
  configuration["manifestVersion"] = 1;
  configuration["source"] = "firmware";
  configuration["sourceFirmware"] = FIRMWARE_VERSION;
  configuration["title"] = "Motor Controller settings";
  configuration["description"] = "Settings, live controls, and hardware information defined by the installed device firmware.";

  JsonObject values = configuration["values"].to<JsonObject>();
  values["wifiSsid"] = settings.wifiSsid;
  values["mqttManaged"] = DeviceMqttEnrollment::isManaged(settings.mqttUser, settings.mqttTopicRoot);
  values["motor1Run"] = motorRunState(0);
  values["motor2Run"] = motorRunState(1);
  values["linkMotorGroups"] = settings.linkMotorGroups;
  values["motor1Direction"] = motorModeText(0);
  values["motor1Speed"] = motors[0].speed;
  values["motor2Direction"] = motorModeText(1);
  values["motor2Speed"] = motors[1].speed;
  for (uint8_t index = 0; index < MOTOR_COUNT; ++index) {
    const String prefix = String("motor") + (index + 1);
    values[prefix + "ArcStart"] = motors[index].arcStart;
    values[prefix + "ArcEnd"] = motors[index].arcEnd;
    values[prefix + "ArcActiveEndpoint"] = motors[index].activeEndpointIsEnd ? "end" : "start";
    values[prefix + "Position"] = motors[index].position;
    values[prefix + "PositionKnown"] = motors[index].positionKnown;
    values[prefix + "Moving"] = motorMoving(index);
  }
  values["holdWhenStopped"] = settings.holdWhenStopped;
  values["stopOnConnectionLoss"] = settings.stopOnConnectionLoss;
  values["motorLed1Assignment"] = settings.motorLedAssignment[0];
  values["motorLed2Assignment"] = settings.motorLedAssignment[1];
  values["motorLed1Testing"] = activeMotorLedTest == 0;
  values["motorLed2Testing"] = activeMotorLedTest == 1;
  values["sensorAnyPresent"] = ranges[0].present || ranges[1].present;
  values["sensorBothPresent"] = ranges[0].present && ranges[1].present;
  for (uint8_t index = 0; index < SENSOR_COUNT; ++index) {
    const String number = String(index + 1);
    values[String("sensor") + number + "Present"] = ranges[index].present;
    values[String("sensor") + number + "Distance"] = ranges[index].distanceMm;
    values[String("sensor") + number + "Threshold"] = ranges[index].thresholdMm;
    values[String("sensor") + number + "Baseline"] = ranges[index].baselineMm;
    values[String("sensor") + number + "Calibrated"] = ranges[index].calibrated;
    values[String("sensor") + number + "Calibrating"] = ranges[index].calibrating;
  }
  values["sensorTriggerEnabled"] = settings.sensorControlMode != SENSOR_CONTROL_DISABLED;
  values["sensorTriggerMode"] = settings.sensorControlMode == SENSOR_CONTROL_ENTER_EXIT ? 2 : 1;
  values["sensorMotorTarget"] = settings.sensorMotorTarget;
  values["sensorRunSeconds"] = settings.sensorRunSeconds;
  values["sensorClearHoldMs"] = settings.sensorClearHoldMs;
  values["externalButtonEnabled"] = settings.begButtonAction != BEG_BUTTON_DISABLED;
  values["externalButtonTarget"] = settings.begButtonAction == BEG_BUTTON_DISABLED
      ? BEG_BUTTON_BOTH_GROUPS : settings.begButtonAction;
  values["externalButtonRunSeconds"] = settings.begButtonRunSeconds;
  values["firmwareVersion"] = FIRMWARE_VERSION;
  values["otaState"] = DeviceOta::lastState;

  JsonArray sections = configuration["sections"].to<JsonArray>();
  auto addSection = [&](const char* id, const char* title, const char* description,
                        const char* layout) -> JsonObject {
    JsonObject section = sections.add<JsonObject>();
    section["id"] = id;
    section["title"] = title;
    section["description"] = description;
    section["layout"] = layout;
    section["fields"].to<JsonArray>();
    return section;
  };
  auto addField = [&](JsonObject section, const char* key, const char* label,
                      const char* type) -> JsonObject {
    JsonObject field = section["fields"].as<JsonArray>().add<JsonObject>();
    field["key"] = key;
    field["label"] = label;
    field["type"] = type;
    return field;
  };
  auto addTextOption = [](JsonObject field, const char* value, const char* label) {
    JsonArray options;
    if (field["options"].is<JsonArray>()) options = field["options"].as<JsonArray>();
    else options = field["options"].to<JsonArray>();
    JsonObject option = options.add<JsonObject>();
    option["value"] = value;
    option["label"] = label;
  };
  auto addNumberOption = [](JsonObject field, int value, const char* label) {
    JsonArray options;
    if (field["options"].is<JsonArray>()) options = field["options"].as<JsonArray>();
    else options = field["options"].to<JsonArray>();
    JsonObject option = options.add<JsonObject>();
    option["value"] = value;
    option["label"] = label;
  };

  JsonObject connection = addSection(
      "connection", "Connection", "Network access is managed automatically by HakoMachi.", "two");
  JsonObject wifi = addField(connection, "wifiSsid", "Wi-Fi network", "status");
  wifi["readOnly"] = true;
  JsonObject messaging = addField(connection, "mqttManaged", "Device messaging", "status");
  messaging["readOnly"] = true;
  messaging["trueLabel"] = "Managed by HakoMachi";
  messaging["falseLabel"] = "Manual configuration";

  JsonObject live = addSection(
      "liveControls", "Live controls", "Start or stop each motor group without changing its saved configuration.", "two");
  JsonObject run1 = addField(live, "motor1Run", "Motor group 1 running", "command-toggle");
  run1["action"] = "set"; run1["control"] = "motor1Run";
  JsonObject run2 = addField(live, "motor2Run", "Motor group 2 running", "command-toggle");
  run2["action"] = "set"; run2["control"] = "motor2Run";

  JsonObject groups = addSection(
      "motorGroups", "Motor groups",
      "Linked groups share Motor Group 1's saved mode, speed, and arc. Disabled groups are skipped by sensor automation.",
      "two");
  addField(groups, "linkMotorGroups", "Link motor groups", "toggle");
  JsonObject direction1 = addField(groups, "motor1Direction", "Motor group 1 direction", "select");
  addTextOption(direction1, "cw", "Clockwise");
  addTextOption(direction1, "ccw", "Counterclockwise");
  addTextOption(direction1, "arc", "Positioned arc");
  addTextOption(direction1, "disabled", "Disabled");
  JsonObject speed1 = addField(groups, "motor1Speed", "Motor group 1 speed", "number");
  speed1["min"] = MIN_SPEED_STEPS_PER_SECOND; speed1["max"] = MAX_SPEED_STEPS_PER_SECOND;
  speed1["step"] = 1; speed1["unit"] = "half-steps/second";
  JsonObject arc1 = addField(groups, "motor1Arc", "Motor group 1 positioned arc", "motor-arc");
  arc1["visibleWhen"]["motor1Direction"] = "arc";
  arc1["motor"] = 1;
  arc1["startKey"] = "motor1ArcStart"; arc1["endKey"] = "motor1ArcEnd";
  arc1["activeKey"] = "motor1ArcActiveEndpoint"; arc1["positionKey"] = "motor1Position";
  arc1["knownKey"] = "motor1PositionKnown"; arc1["movingKey"] = "motor1Moving";
  JsonObject direction2 = addField(groups, "motor2Direction", "Motor group 2 direction", "select");
  direction2["hiddenWhen"]["linkMotorGroups"] = true;
  addTextOption(direction2, "cw", "Clockwise");
  addTextOption(direction2, "ccw", "Counterclockwise");
  addTextOption(direction2, "arc", "Positioned arc");
  addTextOption(direction2, "disabled", "Disabled");
  JsonObject speed2 = addField(groups, "motor2Speed", "Motor group 2 speed", "number");
  speed2["min"] = MIN_SPEED_STEPS_PER_SECOND; speed2["max"] = MAX_SPEED_STEPS_PER_SECOND;
  speed2["step"] = 1; speed2["unit"] = "half-steps/second";
  speed2["hiddenWhen"]["linkMotorGroups"] = true;
  JsonObject arc2 = addField(groups, "motor2Arc", "Motor group 2 positioned arc", "motor-arc");
  arc2["visibleWhen"]["motor2Direction"] = "arc";
  arc2["hiddenWhen"]["linkMotorGroups"] = true;
  arc2["motor"] = 2;
  arc2["startKey"] = "motor2ArcStart"; arc2["endKey"] = "motor2ArcEnd";
  arc2["activeKey"] = "motor2ArcActiveEndpoint"; arc2["positionKey"] = "motor2Position";
  arc2["knownKey"] = "motor2PositionKnown"; arc2["movingKey"] = "motor2Moving";

  JsonObject safety = addSection(
      "motorSafety", "Motor safety", "Choose how the driver behaves while stopped or disconnected.", "two");
  addField(safety, "holdWhenStopped", "Keep coils energized while stopped", "toggle");
  addField(safety, "stopOnConnectionLoss", "Stop motors if MQTT disconnects", "toggle");

  JsonObject leds = addSection(
      "onboardLeds", "Onboard LEDs",
      "Choose whether each motor-status LED is reserved for the board, controlled by JMRI, or shared.", "two");
  const char* ledKeys[] = {"motorLed1Assignment", "motorLed2Assignment"};
  for (uint8_t index = 0; index < MOTOR_COUNT; ++index) {
    const uint8_t objectIndex = index == 0 ? O_M1_LED : O_M2_LED;
    JsonObject led = addField(leds, ledKeys[index], objects[objectIndex].fullId, "output");
    led["help"] = index == 0 ? "GPIO21" : "GPIO5";
    led["resource"] = objects[objectIndex].fullId;
    led["testAction"] = "test";
    led["testControl"] = index == 0 ? "motorLed1" : "motorLed2";
    led["testStateKey"] = index == 0 ? "motorLed1Testing" : "motorLed2Testing";
    led["testTimeoutSeconds"] = OUTPUT_TEST_TIMEOUT_MS / 1000;
    addNumberOption(led, OUTPUT_RESERVED, "Reserved for onboard action");
    addNumberOption(led, OUTPUT_JMRI, "Available for JMRI");
    addNumberOption(led, OUTPUT_SHARED, "Shared");
  }

  JsonObject sensors = addSection(
      "distanceSensors", "Distance sensors",
      "Automatic calibration and live hardware availability. Leave the track clear while calibrating.", "two");
  for (uint8_t index = 0; index < SENSOR_COUNT; ++index) {
    const bool first = index == 0;
    JsonObject sensor = addField(sensors, first ? "sensor1" : "sensor2",
                                  first ? "Sensor 1" : "Sensor 2", "sensor");
    sensor["presentKey"] = first ? "sensor1Present" : "sensor2Present";
    sensor["distanceKey"] = first ? "sensor1Distance" : "sensor2Distance";
    sensor["thresholdKey"] = first ? "sensor1Threshold" : "sensor2Threshold";
    sensor["baselineKey"] = first ? "sensor1Baseline" : "sensor2Baseline";
    sensor["calibratedKey"] = first ? "sensor1Calibrated" : "sensor2Calibrated";
    sensor["calibratingKey"] = first ? "sensor1Calibrating" : "sensor2Calibrating";
    sensor["address"] = first ? "0x30" : "0x31";
    sensor["action"] = "calibrate";
    sensor["control"] = first ? "sensor1" : "sensor2";
    sensor["enabledWhen"][first ? "sensor1Present" : "sensor2Present"] = true;
  }

  JsonObject trigger = addSection(
      "sensorTrigger", "Sensor trigger",
      "Choose how detected trains control the selected motor groups.", "two");
  JsonObject triggerEnabled = addField(trigger, "sensorTriggerEnabled", "Enable sensor trigger", "toggle");
  triggerEnabled["enabledWhen"]["sensorAnyPresent"] = true;
  JsonObject triggerMode = addField(trigger, "sensorTriggerMode", "Trigger", "select");
  triggerMode["visibleWhen"]["sensorTriggerEnabled"] = true;
  addNumberOption(triggerMode, SENSOR_CONTROL_ANY_TIMED, "Any sensor (time-based)");
  addNumberOption(triggerMode, SENSOR_CONTROL_ENTER_EXIT, "Enter-exit sensors");
  JsonArray triggerOptions = triggerMode["options"].as<JsonArray>();
  triggerOptions[0]["enabledWhen"]["sensorAnyPresent"] = true;
  triggerOptions[1]["enabledWhen"]["sensorBothPresent"] = true;
  JsonObject triggerTarget = addField(trigger, "sensorMotorTarget", "Motor target", "select");
  triggerTarget["visibleWhen"]["sensorTriggerEnabled"] = true;
  addNumberOption(triggerTarget, MOTOR_TARGET_BOTH, "Both motor groups");
  addNumberOption(triggerTarget, MOTOR_TARGET_GROUP_1, "Motor group 1");
  addNumberOption(triggerTarget, MOTOR_TARGET_GROUP_2, "Motor group 2");
  JsonObject runTime = addField(trigger, "sensorRunSeconds", "Run time", "number");
  runTime["min"] = MIN_SENSOR_RUN_SECONDS; runTime["max"] = MAX_SENSOR_RUN_SECONDS;
  runTime["step"] = 1; runTime["unit"] = "seconds";
  runTime["visibleWhen"]["sensorTriggerEnabled"] = true;
  runTime["visibleWhen"]["sensorTriggerMode"] = SENSOR_CONTROL_ANY_TIMED;
  JsonObject clearHold = addField(trigger, "sensorClearHoldMs", "Clear hold time", "number");
  clearHold["min"] = 0; clearHold["max"] = MAX_SENSOR_CLEAR_HOLD_MS;
  clearHold["step"] = 50; clearHold["unit"] = "ms";
  clearHold["visibleWhen"]["sensorTriggerEnabled"] = true;
  clearHold["visibleWhen"]["sensorTriggerMode"] = SENSOR_CONTROL_ENTER_EXIT;

  JsonObject button = addSection(
      "externalButton", "External button",
      "Configure the physical button used to run motor groups manually.", "two");
  addField(button, "externalButtonEnabled", "Enable external button", "toggle");
  JsonObject buttonTarget = addField(button, "externalButtonTarget", "Motor target", "select");
  buttonTarget["visibleWhen"]["externalButtonEnabled"] = true;
  addNumberOption(buttonTarget, BEG_BUTTON_BOTH_GROUPS, "Both motor groups");
  addNumberOption(buttonTarget, BEG_BUTTON_GROUP_1, "Motor group 1");
  addNumberOption(buttonTarget, BEG_BUTTON_GROUP_2, "Motor group 2");
  JsonObject buttonTime = addField(button, "externalButtonRunSeconds", "Run time", "number");
  buttonTime["min"] = MIN_SENSOR_RUN_SECONDS; buttonTime["max"] = MAX_SENSOR_RUN_SECONDS;
  buttonTime["step"] = 1; buttonTime["unit"] = "seconds";
  buttonTime["visibleWhen"]["externalButtonEnabled"] = true;

  JsonObject software = addSection(
      "software", "Software", "Installed firmware and update status.", "two");
  JsonObject firmware = addField(software, "firmwareVersion", "Firmware version", "status");
  firmware["readOnly"] = true;
  JsonObject ota = addField(software, "otaState", "Update status", "status");
  ota["readOnly"] = true;
}

bool applyStationConfiguration(JsonObject values) {
  if (values.isNull()) return false;
  stopAllMotors(false);
  if (!values["linkMotorGroups"].isNull()) settings.linkMotorGroups = values["linkMotorGroups"].as<bool>();
  if (!values["motor1Direction"].isNull()) {
    const String direction = values["motor1Direction"].as<String>();
    motors[0].enabled = direction != "disabled";
    motors[0].positionedArc = direction == "arc";
    if (motors[0].enabled && !motors[0].positionedArc) motors[0].reverse = direction == "ccw";
  }
  if (!values["motor1Speed"].isNull()) {
    motors[0].speed = constrain(values["motor1Speed"].as<int>(),
                                MIN_SPEED_STEPS_PER_SECOND, MAX_SPEED_STEPS_PER_SECOND);
  }
  if (settings.linkMotorGroups) {
    motors[1].enabled = motors[0].enabled;
    motors[1].reverse = motors[0].reverse;
    motors[1].speed = motors[0].speed;
    motors[1].positionedArc = motors[0].positionedArc;
    motors[1].activeEndpointIsEnd = motors[0].activeEndpointIsEnd;
    motors[1].arcStart = motors[0].arcStart;
    motors[1].arcEnd = motors[0].arcEnd;
  } else {
    if (!values["motor2Direction"].isNull()) {
      const String direction = values["motor2Direction"].as<String>();
      motors[1].enabled = direction != "disabled";
      motors[1].positionedArc = direction == "arc";
      if (motors[1].enabled && !motors[1].positionedArc) motors[1].reverse = direction == "ccw";
    }
    if (!values["motor2Speed"].isNull()) {
      motors[1].speed = constrain(values["motor2Speed"].as<int>(),
                                  MIN_SPEED_STEPS_PER_SECOND, MAX_SPEED_STEPS_PER_SECOND);
    }
  }
  for (uint8_t index = 0; index < MOTOR_COUNT; ++index) {
    if (settings.linkMotorGroups && index == 1) break;
    const String prefix = String("motor") + (index + 1);
    if (!values[prefix + "ArcStart"].isNull()) motors[index].arcStart = values[prefix + "ArcStart"].as<int32_t>();
    if (!values[prefix + "ArcEnd"].isNull()) motors[index].arcEnd = values[prefix + "ArcEnd"].as<int32_t>();
    if (!values[prefix + "ArcActiveEndpoint"].isNull()) {
      motors[index].activeEndpointIsEnd = values[prefix + "ArcActiveEndpoint"].as<String>() != "start";
    }
  }
  if (settings.linkMotorGroups) {
    motors[1].activeEndpointIsEnd = motors[0].activeEndpointIsEnd;
    motors[1].arcStart = motors[0].arcStart;
    motors[1].arcEnd = motors[0].arcEnd;
  }
  if (!values["holdWhenStopped"].isNull()) settings.holdWhenStopped = values["holdWhenStopped"].as<bool>();
  if (!values["stopOnConnectionLoss"].isNull()) settings.stopOnConnectionLoss = values["stopOnConnectionLoss"].as<bool>();
  if (!values["motorLed1Assignment"].isNull()) {
    settings.motorLedAssignment[0] = constrain(values["motorLed1Assignment"].as<int>(),
                                                (int)OUTPUT_RESERVED, (int)OUTPUT_SHARED);
  }
  if (!values["motorLed2Assignment"].isNull()) {
    settings.motorLedAssignment[1] = constrain(values["motorLed2Assignment"].as<int>(),
                                                (int)OUTPUT_RESERVED, (int)OUTPUT_SHARED);
  }

  bool sensorEnabled = settings.sensorControlMode != SENSOR_CONTROL_DISABLED;
  if (!values["sensorTriggerEnabled"].isNull()) sensorEnabled = values["sensorTriggerEnabled"].as<bool>();
  if (sensorEnabled) {
    settings.sensorControlMode = !values["sensorTriggerMode"].isNull()
        ? static_cast<SensorControlMode>(constrain(values["sensorTriggerMode"].as<int>(),
                                                   (int)SENSOR_CONTROL_ANY_TIMED,
                                                   (int)SENSOR_CONTROL_ENTER_EXIT))
        : (settings.sensorControlMode == SENSOR_CONTROL_DISABLED
            ? SENSOR_CONTROL_ANY_TIMED : settings.sensorControlMode);
  } else {
    settings.sensorControlMode = SENSOR_CONTROL_DISABLED;
  }
  if ((settings.sensorControlMode == SENSOR_CONTROL_ANY_TIMED &&
       !ranges[0].present && !ranges[1].present) ||
      (settings.sensorControlMode == SENSOR_CONTROL_ENTER_EXIT &&
       (!ranges[0].present || !ranges[1].present))) {
    settings.sensorControlMode = SENSOR_CONTROL_DISABLED;
  }
  if (!values["sensorMotorTarget"].isNull()) {
    settings.sensorMotorTarget = static_cast<MotorGroupTarget>(constrain(
        values["sensorMotorTarget"].as<int>(), (int)MOTOR_TARGET_BOTH, (int)MOTOR_TARGET_GROUP_2));
  }
  if (!values["sensorRunSeconds"].isNull()) {
    settings.sensorRunSeconds = constrain(values["sensorRunSeconds"].as<int>(),
                                          MIN_SENSOR_RUN_SECONDS, MAX_SENSOR_RUN_SECONDS);
  }
  if (!values["sensorClearHoldMs"].isNull()) {
    settings.sensorClearHoldMs = constrain(values["sensorClearHoldMs"].as<int>(),
                                           0, (int)MAX_SENSOR_CLEAR_HOLD_MS);
  }

  bool buttonEnabled = settings.begButtonAction != BEG_BUTTON_DISABLED;
  if (!values["externalButtonEnabled"].isNull()) buttonEnabled = values["externalButtonEnabled"].as<bool>();
  if (buttonEnabled) {
    settings.begButtonAction = !values["externalButtonTarget"].isNull()
        ? static_cast<BegButtonAction>(constrain(values["externalButtonTarget"].as<int>(),
                                                 (int)BEG_BUTTON_BOTH_GROUPS,
                                                 (int)BEG_BUTTON_GROUP_2))
        : (settings.begButtonAction == BEG_BUTTON_DISABLED
            ? BEG_BUTTON_BOTH_GROUPS : settings.begButtonAction);
  } else {
    settings.begButtonAction = BEG_BUTTON_DISABLED;
  }
  if (!values["externalButtonRunSeconds"].isNull()) {
    settings.begButtonRunSeconds = constrain(values["externalButtonRunSeconds"].as<int>(),
                                             MIN_SENSOR_RUN_SECONDS, MAX_SENSOR_RUN_SECONDS);
  }
  settings.begButtonRunIndefinitely = false;
  saveAllSettings();
  refreshIndicatorLeds(false);
  return true;
}

void publishStationState() {
  if (!mqtt.connected() || stationStateTopic.isEmpty()) return;
  JsonDocument doc;
  doc["online"] = true;
  doc["deviceType"] = DEVICE_TYPE;
  doc["firmware"] = FIRMWARE_VERSION;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["configCurrent"] = true;
  doc["motor1Run"] = motorRunState(0);
  doc["motor1Direction"] = motors[0].reverse;
  doc["motor1Speed"] = motors[0].speed;
  doc["motor2Run"] = motorRunState(1);
  doc["motor2Direction"] = motors[1].reverse;
  doc["motor2Speed"] = motors[1].speed;
  doc["sensor1"] = ranges[0].occupied;
  doc["sensor2"] = ranges[1].occupied;
  appendStationConfiguration(doc);
  JsonArray capabilities = doc["capabilities"].to<JsonArray>();
  auto addToggle = [&](const char* name, const char* label) {
    JsonObject item = capabilities.add<JsonObject>();
    item["name"] = name; item["label"] = label; item["group"] = "Motor groups";
    item["type"] = "toggle"; item["action"] = "set"; item["stateKey"] = name;
  };
  auto addSpeed = [&](const char* name, const char* label) {
    JsonObject item = capabilities.add<JsonObject>();
    item["name"] = name; item["label"] = label; item["group"] = "Motor groups";
    item["type"] = "range"; item["action"] = "set"; item["stateKey"] = name;
    item["min"] = MIN_SPEED_STEPS_PER_SECOND; item["max"] = MAX_SPEED_STEPS_PER_SECOND; item["step"] = 1;
  };
  addToggle("motor1Run", "Motor group 1"); addToggle("motor1Direction", "Motor group 1 reverse");
  addSpeed("motor1Speed", "Motor group 1 speed");
  addToggle("motor2Run", "Motor group 2"); addToggle("motor2Direction", "Motor group 2 reverse");
  addSpeed("motor2Speed", "Motor group 2 speed");
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(stationStateTopic.c_str(), payload.c_str(), true);
}

bool handleStationCommand(const String& topic, const String& message) {
  if (stationCommandTopic.isEmpty() ||
      (topic != stationCommandTopic && topic != stationBroadcastTopic && topic != stationGroupTopic)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, message)) return true;
  const String action = doc["action"] | "";
  const String control = doc["control"] | "";
  if (action == "configure" && control == "settings") {
    applyStationConfiguration(doc["value"].as<JsonObject>());
  } else if (action == "calibrate" && control == "sensor1") {
    if (ranges[0].present) startRangeCalibration(0);
  } else if (action == "calibrate" && control == "sensor2") {
    if (ranges[1].present) startRangeCalibration(1);
  } else if (action == "stopAll") {
    stopAllMotors();
  } else if (action == "jog" && control.startsWith("motor")) {
    const int motorNumber = control.substring(5).toInt();
    const int32_t steps = constrain(doc["value"] | 0, -50, 50);
    if (motorNumber >= 1 && motorNumber <= MOTOR_COUNT && steps != 0) startFiniteMove(motorNumber - 1, steps);
  } else if (action == "markEndpoint" && control.startsWith("motor")) {
    const int motorNumber = control.substring(5).toInt();
    const String endpoint = doc["value"] | "";
    if (motorNumber >= 1 && motorNumber <= MOTOR_COUNT && (endpoint == "start" || endpoint == "end")) {
      markMotorEndpoint(motorNumber - 1, endpoint == "end");
    }
  } else if (action == "moveEndpoint" && control.startsWith("motor")) {
    const int motorNumber = control.substring(5).toInt();
    const String endpoint = doc["value"] | "";
    if (motorNumber >= 1 && motorNumber <= MOTOR_COUNT && (endpoint == "start" || endpoint == "end")) {
      moveMotorToEndpoint(motorNumber - 1, endpoint == "end");
    }
  } else if (action == "test" && control.startsWith("motorLed")) {
    const int ledNumber = control.substring(8).toInt();
    if (ledNumber >= 1 && ledNumber <= MOTOR_COUNT) {
      setMotorLedTest(static_cast<uint8_t>(ledNumber - 1), doc["value"] | true);
    }
  } else if (action == "set") {
    const bool enabled = doc["value"] | false;
    for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
      const String prefix = String("motor") + (i + 1);
      if (control == prefix + "Run") {
        setConfiguredRun(i, enabled);
      } else if (control == prefix + "Direction") {
        motors[i].reverse = enabled; saveMotorSettings(i);
      } else if (control == prefix + "Speed") {
        motors[i].speed = constrain(doc["value"] | (int)motors[i].speed,
                                    MIN_SPEED_STEPS_PER_SECOND, MAX_SPEED_STEPS_PER_SECOND);
        saveMotorSettings(i);
      }
    }
  }
  publishStationState();
  return true;
}

void mqttCallback(char* topicText, byte* payload, unsigned int length) {
  if (length >= mqtt.getBufferSize()) return;
  String message;
  if (!message.reserve(length + 1)) return;
  for (unsigned int i = 0; i < length; ++i) message += static_cast<char>(payload[i]);
  message.trim();
  String topic(topicText);
  if (handleStationCommand(topic, message)) return;
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
    else if (objects[i].kind == OBJ_LIGHT && objectJmriEnabled(i)) {
      const int8_t ledIndex = i == O_M1_LED ? 0 : (i == O_M2_LED ? 1 : -1);
      if (ledIndex < 0) return;
      bool requested;
      if (message == "ON") requested = true;
      else if (message == "OFF") requested = false;
      else return;
      if (jmriMotorLedState[ledIndex] == requested) return;
      jmriMotorLedState[ledIndex] = requested;
      refreshIndicatorLeds(false);
      // The inbound JMRI topic already reports the requested state. Publishing
      // it again here would be delivered back to this wildcard subscription.
    }
    return;
  }
}

uint16_t automaticThreshold(uint16_t baselineMm) {
  uint16_t changeMm = baselineMm / 8;
  if (changeMm < SENSOR_CALIBRATION_MIN_CHANGE_MM) changeMm = SENSOR_CALIBRATION_MIN_CHANGE_MM;
  return baselineMm > changeMm ? max((uint16_t)20, (uint16_t)(baselineMm - changeMm)) : 20;
}

void startRangeCalibration(uint8_t index) {
  if (index >= SENSOR_COUNT || !ranges[index].present) return;
  RangeState& range = ranges[index];
  range.calibrating = true;
  range.calibrationSampleCount = 0;
  range.occupied = false;
  Serial.printf("Calibrating VL53L0X %u\n", index + 1);
}

void finishRangeCalibration(uint8_t index) {
  RangeState& range = ranges[index];
  uint16_t sorted[SENSOR_CALIBRATION_SAMPLE_COUNT];
  for (uint8_t i = 0; i < SENSOR_CALIBRATION_SAMPLE_COUNT; ++i) sorted[i] = range.calibrationSamples[i];
  for (uint8_t i = 1; i < SENSOR_CALIBRATION_SAMPLE_COUNT; ++i) {
    const uint16_t value = sorted[i];
    int8_t j = i - 1;
    while (j >= 0 && sorted[j] > value) {
      sorted[j + 1] = sorted[j];
      --j;
    }
    sorted[j + 1] = value;
  }
  if ((uint16_t)(sorted[SENSOR_CALIBRATION_SAMPLE_COUNT - 1] - sorted[0]) > SENSOR_CALIBRATION_MAX_SPREAD_MM) {
    range.calibrationSampleCount = 0;
    Serial.printf("VL53L0X %u calibration waiting for a stable clear reading\n", index + 1);
    return;
  }
  range.baselineMm = sorted[SENSOR_CALIBRATION_SAMPLE_COUNT / 2];
  range.thresholdMm = automaticThreshold(range.baselineMm);
  range.calibrated = true;
  range.calibrating = false;
  range.calibrationSampleCount = 0;
  saveThreshold(index);
  publishIntegerObject((index == 0 ? O_S1_OCCUPIED : O_S2_OCCUPIED) + 2, range.thresholdMm);
  Serial.printf("VL53L0X %u calibrated: clear baseline %u mm, occupied threshold %u mm\n",
                index + 1, range.baselineMm, range.thresholdMm);
}

void addRangeCalibrationReading(uint8_t index, uint16_t distanceMm) {
  RangeState& range = ranges[index];
  if (!range.calibrating) return;
  range.calibrationSamples[range.calibrationSampleCount++] = distanceMm;
  if (range.calibrationSampleCount >= SENSOR_CALIBRATION_SAMPLE_COUNT) finishRangeCalibration(index);
}

void initializeRangeSensors() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(25);

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
      if (!ranges[i].calibrated) startRangeCalibration(i);
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
      startConfiguredContinuous(0);
      break;
    case MOTOR_TARGET_GROUP_2:
      startConfiguredContinuous(1);
      break;
    case MOTOR_TARGET_BOTH:
    default:
      startConfiguredContinuous(0);
      startConfiguredContinuous(1);
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
      setConfiguredRun(0, false);
      break;
    case MOTOR_TARGET_GROUP_2:
      setConfiguredRun(1, false);
      break;
    case MOTOR_TARGET_BOTH:
    default:
      for (uint8_t i = 0; i < MOTOR_COUNT; ++i) setConfiguredRun(i, false);
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
    ranges[i].distanceMm = newDistance;

    if (ranges[i].calibrating) {
      ranges[i].occupied = false;
      if (newDistance >= 0) addRangeCalibrationReading(i, (uint16_t)newDistance);
    } else if (newDistance < 0) {
      ranges[i].occupied = false;
    } else if (!ranges[i].occupied && newDistance <= ranges[i].thresholdMm) {
      ranges[i].occupied = true;
    } else if (ranges[i].occupied && newDistance >= ranges[i].thresholdMm + settings.occupiedHysteresisMm) {
      ranges[i].occupied = false;
    }
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
        bool timed = false;
        for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
          if (motors[i].positionedArc) setConfiguredRun(i, !motorAtActiveEndpoint(i));
          else { startContinuous(i); timed = true; }
        }
        begButtonTimerAction = settings.begButtonAction;
        begButtonTimerActive = timed && !settings.begButtonRunIndefinitely;
        begButtonStopAtMs = millis() + static_cast<uint32_t>(settings.begButtonRunSeconds) * 1000UL;
      }
      break;

    case BEG_BUTTON_GROUP_1:
      if (motorMoving(0)) {
        stopMotor(0);
        begButtonTimerActive = false;
      } else {
        if (motors[0].positionedArc) setConfiguredRun(0, !motorAtActiveEndpoint(0));
        else startContinuous(0);
        begButtonTimerAction = settings.begButtonAction;
        begButtonTimerActive = !motors[0].positionedArc && !settings.begButtonRunIndefinitely;
        begButtonStopAtMs = millis() + static_cast<uint32_t>(settings.begButtonRunSeconds) * 1000UL;
      }
      break;

    case BEG_BUTTON_GROUP_2:
      if (motorMoving(1)) {
        stopMotor(1);
        begButtonTimerActive = false;
      } else {
        if (motors[1].positionedArc) setConfiguredRun(1, !motorAtActiveEndpoint(1));
        else startContinuous(1);
        begButtonTimerAction = settings.begButtonAction;
        begButtonTimerActive = !motors[1].positionedArc && !settings.begButtonRunIndefinitely;
        begButtonStopAtMs = millis() + static_cast<uint32_t>(settings.begButtonRunSeconds) * 1000UL;
      }
      break;

    case BEG_BUTTON_DISABLED:
    default:
      break;
  }
}

bool serviceSetupButton() {
  const DeviceBootButton::Event event = setupButton.service();
  if (event == DeviceBootButton::Event::HoldConfirmed) {
    stopAllMotors(false);
    Serial.println(F("BOOT held for 3 seconds: release to enter setup portal"));
  } else if (event == DeviceBootButton::Event::ReleaseConfirmed) {
    startConfigAccessPoint();
  }
  return setupButton.blocksI2c();
}

void serviceBegButtonTimer() {
  if (!begButtonTimerActive || static_cast<int32_t>(millis() - begButtonStopAtMs) < 0) return;

  switch (begButtonTimerAction) {
    case BEG_BUTTON_GROUP_1:
      setConfiguredRun(0, false);
      break;
    case BEG_BUTTON_GROUP_2:
      setConfiguredRun(1, false);
      break;
    case BEG_BUTTON_BOTH_GROUPS:
      for (uint8_t i = 0; i < MOTOR_COUNT; ++i) setConfiguredRun(i, false);
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
  DeviceWifiProvisioning::applyStationTxPower();
  WiFi.begin(settings.wifiSsid, settings.wifiPassword);
}

void onMqttConnected() {
  Serial.println(F("MQTT connected"));
  if (!stationCommandTopic.isEmpty()) {
    mqtt.subscribe(stationCommandTopic.c_str());
    mqtt.subscribe(stationBroadcastTopic.c_str());
    mqtt.subscribe(stationGroupTopic.c_str());
  }
  refreshIndicatorLeds(false);

  // Publish safe current state first so stale retained RUN commands cannot start motors.
  publishAllState();

  String subscription = String(settings.jmriChannel) + "#";
  mqtt.subscribe(subscription.c_str());
  Serial.printf("Subscribed: %s\n", subscription.c_str());

  publishDiscovery();
  publishStationState();
  namesReceived = false;
  requestNames();
}

void serviceMqtt() {
  if (WiFi.status() == WL_CONNECTED) serviceMqttEnrollment();
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
  bool connected;
  if (DeviceMqttEnrollment::isManaged(settings.mqttUser, settings.mqttTopicRoot)) {
    connected = mqtt.connect(clientId.c_str(), settings.mqttUser, settings.mqttPassword,
                             stationStateTopic.c_str(), 1, true, "{\"online\":false}");
  } else {
    connected = strlen(settings.mqttUser) > 0
                  ? mqtt.connect(clientId.c_str(), settings.mqttUser, settings.mqttPassword)
                  : mqtt.connect(clientId.c_str());
  }
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
  if (DeviceMqttEnrollment::isManaged(settings.mqttUser, settings.mqttTopicRoot)) {
    strlcpy(settings.jmriChannel, "hakomachi/jmri/", sizeof(settings.jmriChannel));
    stationStateTopic = String(settings.mqttTopicRoot) + "/state";
    stationCommandTopic = String(settings.mqttTopicRoot) + "/command";
  }
  DeviceOta::begin(settings.jmriChannel, deviceId);
  initializeRangeSensors();
  setupButton.begin(0);

  DeviceWifiProvisioning::configureStation(deviceId);
  initializeConfigPortal();
  if (settings.wifiSsid[0] == '\0') {
    if (!DeviceWifiProvisioning::provision(deviceId)) {
      startConfigAccessPoint();
    }
  } else {
    DeviceWifiProvisioning::applyStationTxPower();
    WiFi.begin(settings.wifiSsid, settings.wifiPassword);
  }
  lastWiFiRetryMs = millis();

  mqtt.setServer(mqttHost, settings.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(16384);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(2);

  Serial.printf("Device: %s (%s)\n", deviceId, fullMac);
  Serial.println(F("Motor outputs safely disabled"));
}

void loop() {
  serviceMotors();
  if (serviceMotorLedTest(millis())) publishStationState();
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





