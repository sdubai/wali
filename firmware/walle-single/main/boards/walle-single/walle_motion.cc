#include "walle_motion.h"
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "ps2_pad.h"
#include "ble_gamepad.h"
#include "gamepad_safety.h"
#include "ps2_servo_control.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr char TAG[] = "WalleMotion";
i2c_master_bus_handle_t i2c_bus;
i2c_master_dev_handle_t pca9685;
std::atomic<bool> pca9685_ready{false};
bool gesture_active = false;
int64_t gesture_started = 0;
std::array<int, 4> gesture_from{};
int gesture_beat = -1;
int64_t drive_deadline_us;
SemaphoreHandle_t drive_mutex;
bool ps2_driving = false; // Protected by drive_mutex.
// Last successful commands, not measured positions (these servos have no
// feedback).
std::atomic<int> servo_angles[8] = {0, 90, 90, 30, 140, 108, 145, 70};

esp_err_t PcaWrite(uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  return i2c_master_transmit(pca9685, data, sizeof(data), 50);
}

bool InitPca9685() {
  if (!i2c_bus) {
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = WALLE_I2C_PORT;
    bus.sda_io_num = WALLE_I2C_SDA;
    bus.scl_io_num = WALLE_I2C_SCL;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    const esp_err_t err = i2c_new_master_bus(&bus, &i2c_bus);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(err));
      return false;
    }
  }

  if (!pca9685) {
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = WALLE_PCA9685_ADDR;
    dev.scl_speed_hz = 100000;
    const esp_err_t err = i2c_master_bus_add_device(i2c_bus, &dev, &pca9685);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to add PCA9685: %s", esp_err_to_name(err));
      return false;
    }
  }

  if (PcaWrite(0x00, 0x30) != ESP_OK || // sleep + auto increment
      PcaWrite(0xFE, 121) != ESP_OK ||  // approximately 50 Hz
      PcaWrite(0x01, 0x04) != ESP_OK || PcaWrite(0x00, 0x20) != ESP_OK) {
    ESP_LOGW(
        TAG,
        "PCA9685 did not respond at address 0x%02X; check SDA/SCL, VCC and GND",
        WALLE_PCA9685_ADDR);
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(10));
  if (PcaWrite(0x00, 0xA0) != ESP_OK)
    return false;
  ESP_LOGI(TAG, "PCA9685 ready at 0x40");
  return true;
}

int LimitServoAngle(int id, int angle) {
#if WALLE_INDEPENDENT_SERVO_CALIBRATION
  if (id >= 1 && id <= 7)
    return std::clamp(angle, 10, 170);
#endif
  int minimum_angle = WALLE_SERVO_MIN_DEG;
  int maximum_angle = WALLE_SERVO_MAX_DEG;
  if (id == 3) {
    minimum_angle = WALLE_SERVO_3_MIN_DEG;
    maximum_angle = WALLE_SERVO_3_MAX_DEG;
  } else if (id == 4) {
    minimum_angle = WALLE_SERVO_4_MIN_DEG;
    maximum_angle = WALLE_SERVO_4_MAX_DEG;
  } else if (id == 5) {
    minimum_angle = WALLE_SERVO_5_MIN_DEG;
    maximum_angle = WALLE_SERVO_5_MAX_DEG;
  } else if (id == 6) {
    minimum_angle = WALLE_SERVO_6_MIN_DEG;
    maximum_angle = WALLE_SERVO_6_MAX_DEG;
  } else if (id == 7) {
    minimum_angle = WALLE_SERVO_7_MIN_DEG;
    maximum_angle = WALLE_SERVO_7_MAX_DEG;
  }
  return std::clamp(angle, minimum_angle, maximum_angle);
}

bool WriteServoAngle(int id, int angle) {
  int physical_angle = angle;
#if WALLE_SERVO_1_REVERSED
  if (id == 1)
    physical_angle = 180 - angle;
#endif

  const int pulse_us = 500 + physical_angle * 2000 / 180;
  const uint16_t ticks = static_cast<uint16_t>(pulse_us * 4096 / 20000);
  const uint8_t reg = static_cast<uint8_t>(0x06 + (id - 1) * 4);
  const uint8_t data[] = {reg, 0, 0, static_cast<uint8_t>(ticks & 0xFF),
                          static_cast<uint8_t>(ticks >> 8)};
  const bool written =
      i2c_master_transmit(pca9685, data, sizeof(data), 50) == ESP_OK;
  if (written && id <= 7)
    servo_angles[id].store(angle);
  return written;
}

bool SetServo(int id, int angle) {
  if (!pca9685_ready || id < 1 || id > WALLE_SERVO_COUNT)
    return false;

  const int requested_angle = angle;
  angle = LimitServoAngle(id, angle);
  if (angle != requested_angle) {
    const int minimum_angle = LimitServoAngle(id, WALLE_SERVO_MIN_DEG - 180);
    const int maximum_angle = LimitServoAngle(id, WALLE_SERVO_MAX_DEG + 180);
    ESP_LOGW(TAG, "Servo %d request %d limited to %d (safe range %d..%d)", id,
             requested_angle, angle, minimum_angle, maximum_angle);
  }

#if WALLE_INDEPENDENT_SERVO_CALIBRATION
  // Deliberately write only the requested channel. Do not home the other
  // neck stage while the operator is determining a new mechanical position.
  if (id <= 7)
    return WriteServoAngle(id, angle);
#endif

  // Mechanical interlock for the two-stage neck pitch linkage. Always return
  // the other stage to level before moving into the opposite direction.
  if (id == 6 && angle != WALLE_SERVO_6_LEVEL_DEG) {
    ESP_LOGI(TAG,
             "Neck interlock: servo 7 -> %d before servo 6 calibration move",
             WALLE_SERVO_7_LEVEL_DEG);
    if (!WriteServoAngle(7, WALLE_SERVO_7_LEVEL_DEG))
      return false;
    vTaskDelay(pdMS_TO_TICKS(60));
  } else if (id == 7 && angle > WALLE_SERVO_7_LEVEL_DEG) {
    ESP_LOGI(TAG, "Neck interlock: servo 6 -> %d before looking up",
             WALLE_SERVO_6_LEVEL_DEG);
    if (!WriteServoAngle(6, WALLE_SERVO_6_LEVEL_DEG))
      return false;
    vTaskDelay(pdMS_TO_TICKS(60));
  }

  // 500..2500 us in a 20 ms frame. Calibrate each servo before mechanical
  // assembly.
  return WriteServoAngle(id, angle);
}

void Drive(int left, int right, int duration_ms, bool from_ps2 = false);
void Stop();

void InitMotors() {
  gpio_config_t output = {};
  output.pin_bit_mask = (1ULL << WALLE_MOTOR_IN1) | (1ULL << WALLE_MOTOR_IN2) |
                        (1ULL << WALLE_MOTOR_IN3) | (1ULL << WALLE_MOTOR_IN4);
  output.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&output));

  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_10_BIT;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = 16000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  for (int channel = 0; channel < 2; ++channel) {
    ledc_channel_config_t config = {};
    config.gpio_num = channel == 0 ? WALLE_MOTOR_ENA : WALLE_MOTOR_ENB;
    config.speed_mode = LEDC_LOW_SPEED_MODE;
    config.channel = static_cast<ledc_channel_t>(channel);
    config.timer_sel = LEDC_TIMER_0;
    config.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&config));
  }
}

void SetOneMotor(int speed, gpio_num_t in_a, gpio_num_t in_b,
                 ledc_channel_t channel) {
  speed = std::clamp(speed, -100, 100);
  gpio_set_level(in_a, speed > 0);
  gpio_set_level(in_b, speed < 0);
  const uint32_t duty = static_cast<uint32_t>(std::abs(speed) * 1023 / 100);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

void Drive(int left, int right, int duration_ms, bool from_ps2) {
  xSemaphoreTake(drive_mutex, portMAX_DELAY);
  if (ps2_driving && !from_ps2) {
    xSemaphoreGive(drive_mutex);
    return;
  }
  if (from_ps2)
    gesture_active = false;
  if (from_ps2 && !ps2_driving) {
    WalleCancelVoiceDrive();
    ps2_driving = true;
  }
  // Chassis wiring discovered by the single-track test:
  // L298N channel A drives the physical right track with reversed polarity.
  // L298N channel B drives the physical left track with normal polarity.
  // Incoming values are always logical physical-left/physical-right speeds.
  const int channel_a = -right;
  const int channel_b = left;
  ESP_LOGD(TAG,
           "Drive logical: left=%d right=%d; outputs: A(right)=%d B(left)=%d; "
           "duration=%dms",
           left, right, channel_a, channel_b, duration_ms);
  SetOneMotor(channel_a, WALLE_MOTOR_IN1, WALLE_MOTOR_IN2, LEDC_CHANNEL_0);
  SetOneMotor(channel_b, WALLE_MOTOR_IN3, WALLE_MOTOR_IN4, LEDC_CHANNEL_1);
  const int timeout = std::clamp(duration_ms, 1, WALLE_DRIVE_TIMEOUT_MS);
  drive_deadline_us = esp_timer_get_time() + timeout * 1000LL;
  xSemaphoreGive(drive_mutex);
}

// Caller holds drive_mutex.
void StopLocked() {
  if (drive_deadline_us)
    ESP_LOGI(TAG, "Drive stopped");
  drive_deadline_us = 0;
  SetOneMotor(0, WALLE_MOTOR_IN1, WALLE_MOTOR_IN2, LEDC_CHANNEL_0);
  SetOneMotor(0, WALLE_MOTOR_IN3, WALLE_MOTOR_IN4, LEDC_CHANNEL_1);
}

void Stop() {
  xSemaphoreTake(drive_mutex, portMAX_DELAY);
  StopLocked();
  xSemaphoreGive(drive_mutex);
}

void PollPs2Servos(bool active, uint16_t pressed, int right_x, int right_y) {
  if (active && (pressed || !Ps2StickCentered(right_x, right_y)))
    gesture_active = false;
  static Ps2PitchSelector pitch;
  static Ps2ServoRamp ramps[8];
  static bool pitch_moving = false;
  static int64_t previous_us = 0;
  static int64_t lower_level_since = 0, upper_level_since = 0;
  const int64_t now = esp_timer_get_time();
  if (servo_angles[6].load() != WALLE_SERVO_6_LEVEL_DEG)
    lower_level_since = 0;
  else if (!lower_level_since)
    lower_level_since = now;
  if (servo_angles[7].load() != WALLE_SERVO_7_LEVEL_DEG)
    upper_level_since = 0;
  else if (!upper_level_since)
    upper_level_since = now;
  // Bound a delayed poll so UART/MQTT delays never create a large angle jump.
  const float dt =
      previous_us ? std::clamp((now - previous_us) / 1000000.0f, 0.0f, 0.05f)
                  : 0.02f;
  previous_us = now;
  if (WALLE_INDEPENDENT_SERVO_CALIBRATION || !active || !pca9685_ready ||
      gesture_active) {
    for (int id : {1, 2, 3, 4, 5, 6, 7})
      ramps[id].Reset(servo_angles[id].load());
    pitch.level = servo_angles[7].load() > WALLE_SERVO_7_LEVEL_DEG   ? 1
                  : servo_angles[6].load() < WALLE_SERVO_6_LEVEL_DEG ? -1
                                                                     : 0;
    pitch.previous_direction = 0;
    pitch_moving = false;
    return;
  }
  auto move = [&](int id, int target, int speed) {
    const int current = servo_angles[id].load();
    const int next = LimitServoAngle(
        id, ramps[id].Advance(current, LimitServoAngle(id, target), dt, speed));
    if (next != current && !WriteServoAngle(id, next))
      ramps[id].Reset(current);
  };
  auto button_pair = [&](int id, uint16_t raise, uint16_t lower,
                         int raised_angle, int lowered_angle, int speed) {
    const int current = servo_angles[id].load();
    const int target = Ps2ButtonTarget(pressed, raise, lower, current,
                                       raised_angle, lowered_angle);
    if (target != current)
      move(id, target, speed);
    else
      ramps[id].Reset(servo_angles[id].load());
  };
  button_pair(1, PS2_L1, PS2_L2, WALLE_SERVO_MAX_DEG, WALLE_SERVO_MIN_DEG,
              WALLE_PS2_ARM_SPEED);
  button_pair(2, PS2_R1, PS2_R2, WALLE_SERVO_MAX_DEG, WALLE_SERVO_MIN_DEG,
              WALLE_PS2_ARM_SPEED);
  // Left eye (servo 4) rises toward 110; right eye (servo 3) rises toward 60.
  button_pair(4, PS2_SQUARE, PS2_CROSS, WALLE_SERVO_4_MIN_DEG,
              WALLE_SERVO_4_MAX_DEG, WALLE_PS2_EYE_SPEED);
  button_pair(3, PS2_TRIANGLE, PS2_CIRCLE, WALLE_SERVO_3_MAX_DEG,
              WALLE_SERVO_3_MIN_DEG, WALLE_PS2_EYE_SPEED);

  const int dx = right_x - 128, dy = right_y - 128;
  const bool horizontal =
      std::abs(dx) > PS2_STICK_DEADZONE && std::abs(dx) > std::abs(dy);
  const bool vertical =
      std::abs(dy) > PS2_STICK_DEADZONE && std::abs(dy) >= std::abs(dx);
  if (horizontal)
    move(5, dx < 0 ? WALLE_SERVO_5_MAX_DEG : WALLE_SERVO_5_MIN_DEG,
         WALLE_PS2_HEAD_SPEED);
  else
    ramps[5].Reset(servo_angles[5].load());

  const int direction = vertical ? (dy < 0 ? 1 : -1) : 0;
  // Sync the selected level to the latest web/voice pose before a new gesture.
  if (!pitch_moving && pitch.previous_direction == 0)
    pitch.level = servo_angles[7].load() > WALLE_SERVO_7_LEVEL_DEG   ? 1
                  : servo_angles[6].load() < WALLE_SERVO_6_LEVEL_DEG ? -1
                                                                     : 0;
  if (pitch.Update(direction)) {
    pitch_moving = true;
    ESP_LOGI(TAG, "PS2 pitch: %s",
             pitch.level > 0   ? "up"
             : pitch.level < 0 ? "down"
                               : "level");
  }
  if (!pitch_moving)
    return;
  // Return the opposite stage to level smoothly BEFORE enabling the other.
  const int lower =
      pitch.level < 0 ? WALLE_SERVO_6_MIN_DEG : WALLE_SERVO_6_LEVEL_DEG;
  const int upper =
      pitch.level > 0 ? WALLE_SERVO_7_MAX_DEG : WALLE_SERVO_7_LEVEL_DEG;
  if (upper > WALLE_SERVO_7_LEVEL_DEG) {
    move(6, WALLE_SERVO_6_LEVEL_DEG, WALLE_PS2_HEAD_SPEED);
    if (lower_level_since && now - lower_level_since >= 120000)
      move(7, upper, WALLE_PS2_HEAD_SPEED);
  } else {
    move(7, WALLE_SERVO_7_LEVEL_DEG, WALLE_PS2_HEAD_SPEED);
    if (upper_level_since && now - upper_level_since >= 120000)
      move(6, lower, WALLE_PS2_HEAD_SPEED);
  }
  if (servo_angles[6].load() == lower && servo_angles[7].load() == upper)
    pitch_moving = false;
}

void PollPs2() {
  static int previous_source = 0;
  static walle::GamepadArmGate arm_gate;
  uint16_t pressed = 0;
  int left_x = 128, left_y = 128, right_x = 128, right_y = 128;
  const bool ps2_valid = ReadPs2Pad(pressed, left_x, left_y, right_x, right_y);
  walle::GamepadState ble;
  const bool ble_valid = ReadBleGamepad(ble);
  const int source = ble_valid ? 2 : ps2_valid ? 1 : 0;
  const bool valid = source != 0;
  if (ble_valid) {
    pressed = ble.buttons;
    left_x = ble.lx;
    left_y = ble.ly;
    right_x = ble.rx;
    right_y = ble.ry;
  }
  if (source != previous_source) {
    ESP_LOGI(TAG, "Controller: %s; release buttons and center sticks to arm",
             source == 2   ? "X3S BLE"
             : source == 1 ? "PS2"
                           : "disconnected");
    previous_source = source;
  }
  const bool armed =
      arm_gate.Update(source, !pressed && Ps2StickCentered(left_x, left_y) &&
                                  Ps2StickCentered(right_x, right_y));
  static int64_t next_ble_log = 0;
  if (ble_valid && esp_timer_get_time() >= next_ble_log) {
    ESP_LOGI(TAG, "BLE input armed=%d buttons=%04x LX=%d LY=%d RX=%d RY=%d",
             armed, pressed, left_x, left_y, right_x, right_y);
    next_ble_log = esp_timer_get_time() + 3000000;
  }
  int left = 0, right = 0;
  if (valid && armed) {
    if (pressed & 0xf0)
      Ps2TrackSpeeds(pressed, WALLE_PS2_SPEED, left, right);
    else
      Ps2StickSpeeds(left_x, left_y, WALLE_PS2_SPEED, left, right);
  }
  if (left || right) {
    Drive(left, right, WALLE_PS2_TIMEOUT_MS, true);
  } else {
    xSemaphoreTake(drive_mutex, portMAX_DELAY);
    if (ps2_driving) {
      StopLocked();
      ps2_driving = false;
    }
    xSemaphoreGive(drive_mutex);
  }
  PollPs2Servos(valid && armed, pressed, right_x, right_y);
}

struct LocalCommand {
  unsigned epoch;
  char text[256];
};
std::atomic<unsigned> command_epoch{0};
QueueHandle_t local_queue;
std::atomic<bool> emergency_stop{false};

void StopGesture() {
  gesture_active = false;
  gesture_beat = -1;
}
void StartGesture() {
  Stop();
  if (!pca9685_ready.load()) {
    ESP_LOGW(
        TAG,
        "Test gesture skipped: PCA9685 not ready; audio test can still play");
    return;
  }
  ESP_LOGI(TAG, "Starting arm/eye test gesture (servos 1..4 only)");
  // Only arms and eyes. Reassembled neck and tracks stay untouched.
  for (int i = 0; i < 4; ++i)
    gesture_from[i] = servo_angles[i + 1].load();
  gesture_started = esp_timer_get_time();
  gesture_beat = -1;
  gesture_active = true;
}
void TickGesture() {
  if (!gesture_active || !pca9685_ready.load())
    return;
  constexpr int poses[][4] = {{90, 90, 45, 125}, {78, 90, 52, 118},
                              {86, 90, 48, 122}, {90, 102, 52, 118},
                              {90, 94, 48, 122}, {90, 90, 30, 140}};
  constexpr int beat_ms = 1500;
  const int elapsed = (esp_timer_get_time() - gesture_started) / 1000;
  const int beat = elapsed / beat_ms;
  if (beat >= 6) {
    StopGesture();
    return;
  }
  if (gesture_beat != beat) {
    for (int i = 0; i < 4; ++i)
      gesture_from[i] = servo_angles[i + 1].load();
    gesture_beat = beat;
  }
  const float t = float(elapsed % beat_ms) / beat_ms;
  const float blend = t * t * (3 - 2 * t);
  for (int i = 0; i < 4; ++i) {
    int angle = std::lround(gesture_from[i] +
                            (poses[beat][i] - gesture_from[i]) * blend);
    if (i == 2)
      angle = std::clamp(angle, WALLE_SERVO_3_MIN_DEG, WALLE_SERVO_3_MAX_DEG);
    if (i == 3)
      angle = std::clamp(angle, WALLE_SERVO_4_MIN_DEG, WALLE_SERVO_4_MAX_DEG);
    if (angle != servo_angles[i + 1].load() && !WriteServoAngle(i + 1, angle)) {
      StopGesture();
      return;
    }
  }
}
void ExecuteLocal(const char *line) {
  cJSON *root = cJSON_Parse(line);
  if (!root)
    return;
  const auto *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
  if (cJSON_IsString(cmd)) {
    if (!strcmp(cmd->valuestring, "drive")) {
      StopGesture();
      const auto *l = cJSON_GetObjectItem(root, "left");
      const auto *r = cJSON_GetObjectItem(root, "right");
      const auto *d = cJSON_GetObjectItem(root, "duration");
      if (cJSON_IsNumber(l) && cJSON_IsNumber(r))
        Drive(l->valueint, r->valueint, cJSON_IsNumber(d) ? d->valueint : 750);
    } else if (!strcmp(cmd->valuestring, "servo")) {
      StopGesture();
      const auto *id = cJSON_GetObjectItem(root, "id");
      const auto *a = cJSON_GetObjectItem(root, "angle");
      if (cJSON_IsNumber(id) && cJSON_IsNumber(a))
        SetServo(id->valueint, a->valueint);
    } else if (!strcmp(cmd->valuestring, "action")) {
      const auto *name = cJSON_GetObjectItem(root, "name");
      if (cJSON_IsString(name) && !strcmp(name->valuestring, "introduce"))
        StartGesture();
    }
  }
  cJSON_Delete(root);
}
void LocalMotionTask(void *) {
  int64_t next_pca_retry = 0;
  for (;;) {
    LocalCommand command{};
    const bool received =
        xQueueReceive(local_queue, &command, pdMS_TO_TICKS(20)) == pdTRUE;
    if (emergency_stop.exchange(false)) {
      StopGesture();
      Stop();
    }
    if (received && command.epoch == command_epoch.load())
      ExecuteLocal(command.text);
    if (!pca9685_ready.load() && esp_timer_get_time() >= next_pca_retry) {
      pca9685_ready.store(InitPca9685());
      next_pca_retry = esp_timer_get_time() + 5000000;
    }
    PollPs2();
    TickGesture();
    xSemaphoreTake(drive_mutex, portMAX_DELAY);
    if (drive_deadline_us && esp_timer_get_time() >= drive_deadline_us)
      StopLocked();
    xSemaphoreGive(drive_mutex);
  }
}
} // namespace

void WalleMotionStart() {
  drive_mutex = xSemaphoreCreateMutex();
  local_queue = xQueueCreate(1, sizeof(LocalCommand));
  assert(drive_mutex && local_queue);
  InitMotors();
  Stop();
  InitPs2Pad();
  InitBleGamepad();
  ESP_ERROR_CHECK(xTaskCreate(LocalMotionTask, "local_motion", 6144, nullptr, 4,
                              nullptr) == pdPASS
                      ? ESP_OK
                      : ESP_ERR_NO_MEM);
}
void WalleMotionStop() {
  command_epoch.fetch_add(1);
  if (local_queue)
    xQueueReset(local_queue);
  emergency_stop.store(true);
}
void WalleMotionSubmit(const char *line) {
  if (!line || !local_queue || strlen(line) >= sizeof(LocalCommand::text))
    return;
  cJSON *root = cJSON_Parse(line);
  if (!root)
    return;
  const auto *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
  const bool stop = cJSON_IsString(cmd) && !strcmp(cmd->valuestring, "stop");
  cJSON_Delete(root);
  if (stop) {
    WalleMotionStop();
    return;
  }
  LocalCommand command{};
  command.epoch = command_epoch.load();
  strlcpy(command.text, line, sizeof(command.text));
  xQueueOverwrite(local_queue, &command);
}
