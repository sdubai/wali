#pragma once

#include <algorithm>
#include <cmath>

// Move one discrete pitch level per stick deflection, requiring a return to
// the deadzone before repeating. Holding the stick cannot skip level.
struct Ps2PitchSelector {
    int level = 0;  // -1 down, 0 level, +1 up
    int previous_direction = 0;

    bool Update(int direction) {
        const int before = level;
        if (direction != 0 && previous_direction == 0)
            level = std::clamp(level + direction, -1, 1);
        previous_direction = direction;
        return level != before;
    }
};

// Fractional motion accumulates across polls; acceleration and braking avoid
// sending an endpoint immediately. The current value is the last successful
// PWM command, not measured servo feedback.
struct Ps2ServoRamp {
    float position = 0;
    float velocity = 0;
    bool initialized = false;

    void Reset(int current) {
        position = current;
        velocity = 0;
        initialized = true;
    }

    int Advance(int current, int target, float dt, float max_speed) {
        if (!initialized || std::lround(position) != current) Reset(current);
        if (current == target) { Reset(current); return current; }
        const float distance = target - position;
        constexpr float acceleration = 100.0f;
        const float desired = std::copysign(
            std::min(max_speed, std::sqrt(2 * acceleration * std::abs(distance))), distance);
        velocity += std::clamp(desired - velocity, -acceleration * dt, acceleration * dt);
        const float next = position + velocity * dt;
        if ((distance > 0 && next >= target) || (distance < 0 && next <= target)) {
            position = target;
            velocity = 0;
        } else position = next;
        const int command = static_cast<int>(std::lround(position));
        if (command == target) {
            position = target;
            velocity = 0;
        }
        return command;
    }
};
