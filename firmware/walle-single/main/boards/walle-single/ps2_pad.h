#pragma once
#include <cstdint>

// Poll from one task only. False means missing or malformed response.
void InitPs2Pad();
bool ReadPs2Pad(uint16_t& pressed, int& left_x, int& left_y, int& right_x, int& right_y);

constexpr uint16_t PS2_L2 = 1u << 8;
constexpr uint16_t PS2_R2 = 1u << 9;
constexpr uint16_t PS2_L1 = 1u << 10;
constexpr uint16_t PS2_R1 = 1u << 11;
constexpr uint16_t PS2_TRIANGLE = 1u << 12;
constexpr uint16_t PS2_CIRCLE = 1u << 13;
constexpr uint16_t PS2_CROSS = 1u << 14;
constexpr uint16_t PS2_SQUARE = 1u << 15;
constexpr uint16_t PS2_SERVO_BUTTONS = PS2_L1 | PS2_L2 | PS2_R1 | PS2_R2 |
    PS2_TRIANGLE | PS2_CIRCLE | PS2_CROSS | PS2_SQUARE;

// Press-and-hold pairs: opposite buttons together mean hold, including mirrored eyes.
inline int Ps2ButtonTarget(uint16_t pressed, uint16_t raise, uint16_t lower,
                          int current, int raised_angle, int lowered_angle) {
    const bool up = pressed & raise, down = pressed & lower;
    return up == down ? current : up ? raised_angle : lowered_angle;
}

constexpr int PS2_STICK_DEADZONE = 32;
inline bool Ps2StickCentered(int x, int y) {
    return x >= 128 - PS2_STICK_DEADZONE && x <= 128 + PS2_STICK_DEADZONE &&
           y >= 128 - PS2_STICK_DEADZONE && y <= 128 + PS2_STICK_DEADZONE;
}

// Dominant axis selects one direction, matching the four direction buttons.
inline void Ps2StickSpeeds(int x, int y, int maximum, int& left, int& right) {
    const int dx = x - 128, dy = y - 128;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    const int magnitude = ax > ay ? ax : ay;
    left = right = 0;
    if (magnitude <= PS2_STICK_DEADZONE) return;
    int speed = (magnitude - PS2_STICK_DEADZONE) * maximum /
                (127 - PS2_STICK_DEADZONE);
    if (speed > maximum) speed = maximum;
    if (ay >= ax) left = right = dy < 0 ? speed : -speed;
    else { left = dx > 0 ? speed : -speed; right = -left; }
}

// Multiple direction keys deliberately mean stop; no diagonal driving yet.
inline void Ps2TrackSpeeds(uint16_t pressed, int speed, int& left, int& right) {
    left = right = 0;
    switch (pressed & 0x00f0) {
        case 0x10: left = right = speed; break;
        case 0x40: left = right = -speed; break;
        case 0x80: left = -speed; right = speed; break;
        case 0x20: left = speed; right = -speed; break;
    }
}
