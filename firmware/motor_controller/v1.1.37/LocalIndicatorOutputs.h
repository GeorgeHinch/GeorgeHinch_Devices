#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Logical state is separate from the local identify waveform. No networking,
// storage, motor control, or serial I/O is allowed in the indicator task.
struct IndicatorState {
  uint8_t normalMask = 0;
  int8_t testIndex = -1;
  uint32_t testStarted = 0;
  bool ended = false;
  void test(uint8_t index, bool enabled, uint32_t now) {
    if (enabled) { testIndex = index; testStarted = now; }
    else if (testIndex == index) testIndex = -1;
    ended = false;
  }
  uint8_t step(uint32_t now) {
    uint8_t mask = normalMask;
    if (testIndex >= 0) {
      const uint32_t elapsed = now - testStarted;
      if (elapsed >= 30000) { testIndex = -1; ended = true; }
      else {
        const uint8_t bit = static_cast<uint8_t>(1U << testIndex);
        mask = (elapsed / 350) % 2 == 0 ? (mask | bit) : (mask & ~bit);
      }
    }
    return mask;
  }
};

class LocalIndicatorOutputs {
 public:
  struct Snapshot { uint8_t normalMask; uint8_t physicalMask; int8_t testIndex; bool ready; };
  bool begin(const uint8_t* pins, uint8_t count, uint8_t initialMask = 0) {
    if (count == 0 || count > 8 || task_ != nullptr) return false;
    count_ = count;
    state_.normalMask = initialMask;
    for (uint8_t i = 0; i < count_; ++i) {
      pins_[i] = pins[i];
      pinMode(pins_[i], OUTPUT);
      digitalWrite(pins_[i], LOW);
    }
    return xTaskCreate(run, "indicator-output", 2048, this, 2, &task_) == pdPASS;
  }
  void set(uint8_t index, bool on) {
    if (index >= count_) return;
    portENTER_CRITICAL(&mux_);
    const uint8_t bit = static_cast<uint8_t>(1U << index);
    state_.normalMask = on ? state_.normalMask | bit : state_.normalMask & ~bit;
    portEXIT_CRITICAL(&mux_);
  }
  void test(uint8_t index, bool enabled) {
    if (index >= count_ || task_ == nullptr) return;
    portENTER_CRITICAL(&mux_);
    state_.test(index, enabled, millis());
    portEXIT_CRITICAL(&mux_);
  }
  Snapshot snapshot() {
    portENTER_CRITICAL(&mux_);
    Snapshot result{state_.normalMask, physicalMask_, state_.testIndex, task_ != nullptr};
    portEXIT_CRITICAL(&mux_);
    return result;
  }
  bool consumeEnded() {
    portENTER_CRITICAL(&mux_);
    const bool ended = state_.ended;
    state_.ended = false;
    portEXIT_CRITICAL(&mux_);
    return ended;
  }
  // Called only while building the HTTP response; never from MQTT snapshots.
  void appendLive(JsonObject values, const String& key, uint8_t index, const char* source) {
    const Snapshot s = snapshot();
    const bool testing = s.testIndex == index;
    const bool on = (s.normalMask & (1U << index)) != 0;
    values[key + "Commanded"] = testing || on ? "On" : "Off";
    values[key + "Source"] = testing ? "Output test" : source;
    values[key + "Effect"] = testing ? "Test blink" : on ? "Steady" : "None";
    values[key + "Level"] = (s.physicalMask & (1U << index)) ? "High" : "Low";
    values[key + "Timing"] = s.ready ? "Running" : "Unavailable (outputs held off)";
  }
  static void appendFields(JsonObject section, const String& key, const char* label) {
    JsonObject field = section["fields"].as<JsonArray>().add<JsonObject>();
    field["key"] = key + "Status";
    field["label"] = label;
    field["type"] = "status-group";
    field["readOnly"] = true;
    JsonArray items = field["items"].to<JsonArray>();
    const char* suffixes[] = {"Commanded", "Source", "Effect", "Level", "Timing"};
    const char* labels[] = {"Commanded state", "Controlling source", "Effect", "Output level (not measured)", "Output timing"};
    for (uint8_t i = 0; i < 5; ++i) {
      JsonObject item = items.add<JsonObject>();
      item["key"] = key + suffixes[i]; item["label"] = labels[i];
    }
  }
 private:
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t task_ = nullptr;
  IndicatorState state_;
  uint8_t pins_[8] = {};
  uint8_t count_ = 0;
  uint8_t physicalMask_ = 0;
  static void run(void* context) {
    static_cast<LocalIndicatorOutputs*>(context)->service();
  }
  void service() {
    uint16_t previous = 256;
    for (;;) {
      portENTER_CRITICAL(&mux_);
      const uint8_t mask = state_.step(millis());
      portEXIT_CRITICAL(&mux_);
      if (mask != previous) {
        for (uint8_t i = 0; i < count_; ++i) digitalWrite(pins_[i], mask & (1U << i) ? HIGH : LOW);
        previous = mask;
        portENTER_CRITICAL(&mux_);
        physicalMask_ = mask;
        portEXIT_CRITICAL(&mux_);
      }
      vTaskDelay(pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1);
    }
  }
};
