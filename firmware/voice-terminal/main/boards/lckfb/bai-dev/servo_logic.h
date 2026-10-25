#pragma once
#include <string_view>
#include <cstdint>
#include "servo_config.h"

namespace servo_logic {
inline bool Valid(int channel, int angle) {
    return channel >= 0 && channel < servo_config::kServoCount && angle >= servo_config::kMinAngle &&
           angle <= servo_config::kMaxAngle;
}
inline bool ParseAngle(std::string_view text, int& angle) {
    if (text.empty() || text.size() > 3) return false;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + c - '0';
    }
    if (!Valid(0, value)) return false;
    angle = value;
    return true;
}
inline int PulseUs(int channel, int angle) {
    return servo_config::kMinPulseUs[channel] +
        (servo_config::kMaxPulseUs[channel] - servo_config::kMinPulseUs[channel]) * angle / 180;
}
inline uint16_t Ticks(int channel, int angle) {
    const int64_t numerator = int64_t(PulseUs(channel, angle)) * servo_config::kOscillatorHz;
    const int64_t denominator = int64_t(servo_config::kPrescale + 1) * 1000000;
    return static_cast<uint16_t>((numerator + denominator / 2) / denominator);
}
}
