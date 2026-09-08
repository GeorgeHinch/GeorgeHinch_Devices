#pragma once

#include <Arduino.h>

// GPIO9 is shared with I2C SCL on the sensor boards.  This polls the pin as an
// input only; it deliberately never changes pin mode and never uses interrupts.
namespace DeviceBootButton {

enum class Event : uint8_t { None, HoldConfirmed, ReleaseConfirmed };

class HoldProtection {
 public:
  explicit HoldProtection(uint8_t pin) : pin_(pin) {}

  void begin(uint32_t normalBootStartedMs = 0) {
    bootStartedMs_ = normalBootStartedMs;
    holdStartedMs_ = 0;
    releaseStartedMs_ = 0;
    state_ = State::WaitingToArm;
  }

  Event service(uint32_t now = millis()) {
    if (state_ == State::WaitingToArm) {
      if (now - bootStartedMs_ < ARM_DELAY_MS) return Event::None;
      state_ = State::Monitoring;
    }

    if (state_ == State::Monitoring) {
      if (digitalRead(pin_) != LOW) {
        holdStartedMs_ = 0;
        return Event::None;
      }
      if (holdStartedMs_ == 0) {
        holdStartedMs_ = now;
        return Event::None;
      }
      if (now - holdStartedMs_ < HOLD_DURATION_MS) return Event::None;

      state_ = State::WaitingForRelease;
      releaseStartedMs_ = 0;
      return Event::HoldConfirmed;
    }

    if (state_ == State::WaitingForRelease) {
      if (digitalRead(pin_) == LOW) {
        releaseStartedMs_ = 0;
        return Event::None;
      }
      if (releaseStartedMs_ == 0) {
        releaseStartedMs_ = now;
        return Event::None;
      }
      if (now - releaseStartedMs_ < RELEASE_DEBOUNCE_MS) return Event::None;

      state_ = State::PortalLaunched;
      return Event::ReleaseConfirmed;
    }

    return Event::None;
  }

  bool blocksI2c() const {
    return state_ == State::WaitingForRelease || state_ == State::PortalLaunched;
  }

 private:
  static constexpr uint32_t ARM_DELAY_MS = 3000;
  static constexpr uint32_t HOLD_DURATION_MS = 3000;
  static constexpr uint32_t RELEASE_DEBOUNCE_MS = 50;

  enum class State : uint8_t { WaitingToArm, Monitoring, WaitingForRelease, PortalLaunched };

  uint8_t pin_;
  uint32_t bootStartedMs_ = 0;
  uint32_t holdStartedMs_ = 0;
  uint32_t releaseStartedMs_ = 0;
  State state_ = State::WaitingToArm;
};

}  // namespace DeviceBootButton
