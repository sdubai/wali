#pragma once

// Independent of XiaoZhi's voice transport. Never put Dashboard credentials here.
namespace servo_config {
inline constexpr char kBroker[] = "mqtt://192.168.31.248:1883";
inline constexpr char kUsername[] = "";
inline constexpr char kPassword[] = "";
inline constexpr char kPrefix[] = "walle/esp32_01";
inline constexpr unsigned kAddress = 0x40;
inline constexpr int kMinAngle = 60;
inline constexpr int kMaxAngle = 120;
inline constexpr int kMinPulseUs[4] = {500, 500, 500, 500};
inline constexpr int kMaxPulseUs[4] = {2500, 2500, 2500, 2500};
// Nominal PCA9685 oscillator; calibrate only from measured pulse timing.
inline constexpr int kOscillatorHz = 25000000;
inline constexpr int kPrescale = 121; // approximately 50Hz
}
