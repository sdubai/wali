#pragma once
#include "ble_hid_gamepad.h"

namespace walle {
class GamepadSample {
  GamepadState state_;
  int64_t received_us_ = 0;
  bool available_ = false;

public:
  static constexpr int64_t kFreshUs = 350000;
  void Release() { available_ = false; }
  void Publish(const GamepadState &state, int64_t now) {
    state_ = state;
    received_us_ = now;
    available_ = true;
  }
  bool Read(GamepadState &state, int64_t now) const {
    if (!available_ || now < received_us_ || now - received_us_ > kFreshUs)
      return false;
    state = state_;
    return true;
  }
};
class GamepadArmGate {
  int source_ = 0;
  bool armed_ = false;

public:
  bool Update(int source, bool neutral) {
    if (source != source_)
      armed_ = false;
    source_ = source;
    if (!source)
      armed_ = false;
    else if (neutral)
      armed_ = true;
    return armed_;
  }
};
} // namespace walle
