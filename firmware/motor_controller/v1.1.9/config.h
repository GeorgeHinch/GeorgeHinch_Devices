#pragma once

#include <Arduino.h>

// MQTT Motor Controller hardware revision: ESP32-C3 SuperMini + 74HC595.
constexpr uint8_t PIN_SHIFT_DATA  = 1;   // U1 SER / DS
constexpr uint8_t PIN_SHIFT_CLOCK = 0;   // U1 SHCP / SRCLK
constexpr uint8_t PIN_SHIFT_LATCH = 4;   // U1 STCP / RCLK
constexpr uint8_t PIN_SHIFT_OE_N  = 20;  // U1 /OE, active low

constexpr uint8_t PIN_MOTOR1_LED = 21;
constexpr uint8_t PIN_MOTOR2_LED = 5;
constexpr uint8_t PIN_BEG_BUTTON = 3;    // Active low
constexpr uint8_t PIN_BEG_LED    = 10;

constexpr uint8_t PIN_I2C_SDA = 8;
constexpr uint8_t PIN_SETUP_BUTTON = 9;  // Onboard BOOT; shared with I2C SCL
constexpr uint8_t PIN_I2C_SCL = PIN_SETUP_BUTTON;
constexpr uint8_t PIN_XSHUT_1 = 7;
constexpr uint8_t PIN_XSHUT_2 = 6;

constexpr uint8_t MOTOR_COUNT = 2;
constexpr uint8_t SENSOR_COUNT = 2;

// 28BYJ-48 half-step sequence. Motor 1 occupies QA-QD; Motor 2 occupies QE-QH.
constexpr uint8_t HALF_STEP_SEQUENCE[8] = {
  0b0001, 0b0011, 0b0010, 0b0110,
  0b0100, 0b1100, 0b1000, 0b1001
};

// Tune these after the first bench test.
constexpr uint16_t DEFAULT_SPEED_STEPS_PER_SECOND = 500;
constexpr uint16_t MIN_SPEED_STEPS_PER_SECOND = 50;
constexpr uint16_t MAX_SPEED_STEPS_PER_SECOND = 1000;
constexpr bool INVERT_MOTOR_DIRECTION[MOTOR_COUNT] = { false, false };
constexpr bool DEFAULT_LINK_MOTOR_GROUPS = true;
constexpr bool DEFAULT_HOLD_WHEN_STOPPED = false;

// Safer default: a remotely-started motor stops if the MQTT session is lost.
constexpr bool DEFAULT_STOP_MOTORS_ON_CONNECTION_LOSS = true;

// Local button behavior: press once to run both channels, press again to stop.
enum BegButtonAction : uint8_t {
  BEG_BUTTON_DISABLED = 0,
  BEG_BUTTON_BOTH_GROUPS = 1,
  BEG_BUTTON_GROUP_1 = 2,
  BEG_BUTTON_GROUP_2 = 3
};
constexpr BegButtonAction DEFAULT_BEG_BUTTON_ACTION = BEG_BUTTON_DISABLED;
constexpr bool DEFAULT_BEG_BUTTON_RUN_INDEFINITELY = false;
constexpr uint16_t DEFAULT_BEG_BUTTON_RUN_SECONDS = 30;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t SETUP_BUTTON_HOLD_MS = 3000;

enum SensorControlMode : uint8_t {
  SENSOR_CONTROL_DISABLED = 0,
  SENSOR_CONTROL_ANY_TIMED = 1,
  SENSOR_CONTROL_ENTER_EXIT = 2
};

enum MotorGroupTarget : uint8_t {
  MOTOR_TARGET_BOTH = 0,
  MOTOR_TARGET_GROUP_1 = 1,
  MOTOR_TARGET_GROUP_2 = 2
};

constexpr SensorControlMode DEFAULT_SENSOR_CONTROL_MODE = SENSOR_CONTROL_DISABLED;
constexpr MotorGroupTarget DEFAULT_SENSOR_MOTOR_TARGET = MOTOR_TARGET_BOTH;
constexpr uint16_t DEFAULT_SENSOR_RUN_SECONDS = 30;
constexpr uint16_t MIN_SENSOR_RUN_SECONDS = 1;
constexpr uint16_t MAX_SENSOR_RUN_SECONDS = 3600;
constexpr uint32_t ENTER_EXIT_GUARD_MS = 500;
constexpr uint16_t DEFAULT_SENSOR_CLEAR_HOLD_MS = 500;
constexpr uint16_t MAX_SENSOR_CLEAR_HOLD_MS = 20000;

// VL53L0X breakouts share I2C. XSHUT is released one sensor at a time so each
// module can receive a unique address. Never drive XSHUT high from the ESP32.
constexpr uint8_t SENSOR_I2C_ADDRESS[SENSOR_COUNT] = { 0x30, 0x31 };
constexpr uint16_t DEFAULT_SENSOR_SAMPLE_PERIOD_MS = 100;
constexpr uint16_t DEFAULT_OCCUPIED_THRESHOLD_MM = 250;
constexpr uint16_t DEFAULT_OCCUPIED_HYSTERESIS_MM = 20;
constexpr uint8_t SENSOR_CALIBRATION_SAMPLE_COUNT = 25;
constexpr uint16_t SENSOR_CALIBRATION_MAX_SPREAD_MM = 60;
constexpr uint16_t SENSOR_CALIBRATION_MIN_CHANGE_MM = 60;
constexpr uint16_t SENSOR_MIN_PUBLISH_CHANGE_MM = 5;
constexpr uint32_t SENSOR_HEARTBEAT_MS = 5000;

// The configuration web server is always available on the station IP. A
// captive setup access point starts if Wi-Fi is still unavailable after this
// delay, or when the onboard BOOT button is held for three seconds while running.
constexpr uint32_t CONFIG_PORTAL_FALLBACK_MS = 30000;

constexpr uint32_t WIFI_RETRY_MS = 10000;
constexpr uint32_t MQTT_RETRY_MS = 5000;
constexpr uint32_t NAME_RETRY_MS = 10000;
