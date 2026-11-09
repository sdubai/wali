#pragma once

#include <atomic>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mqtt_client.h>
#include <mutex>
#include <string>

class MotionLink {
public:
  MotionLink();
  ~MotionLink() = default;

  static void CancelLocalDrive();
  void RegisterTools();
  void StartNetwork();
  // Local fallback for models that emit tool-call markup as plain text.
  static void ArmIntroductionFromSpeech();
  static bool StartPendingIntroductionWithTts();
  static bool TriggerIntroductionFromSpeech();

private:
  enum class CommandType : unsigned char {
    kDrive,
    kServo,
    kAction,
    kStop,
    kCancelDrive,
  };

  struct Command {
    CommandType type;
    int first;
    int second;
    int duration_ms;
  };

  QueueHandle_t queue_ = nullptr;
  std::atomic<bool> stop_pending_{false};
  std::atomic<bool> cancel_drive_pending_{false};
  esp_mqtt_client_handle_t mqtt_ = nullptr;
  std::atomic<bool> mqtt_connected_{false};
  std::mutex mqtt_mutex_;
  httpd_handle_t config_server_ = nullptr;
  std::string mqtt_uri_;
  std::string mqtt_username_;
  std::string mqtt_password_;
  void StartConfigServer();
  void ConnectMqtt();
  static esp_err_t ConfigPage(httpd_req_t *request);
  static esp_err_t SaveConfig(httpd_req_t *request);
  static MotionLink *instance_;
  int64_t introduction_pending_until_us_ = 0;

  bool Submit(const Command &command, bool urgent = false);
  void Run();
  void ReceiveTelemetry();
  void ForwardTelemetry(const char *line);
  void SendPiWifiConfig(const char *request_id);
  void SendDrive(int left, int right, int duration_ms);
  void SendServo(int servo, int angle);
  void SendAction(const char *name);
  void SendStop();
  void WriteLine(const char *line);
  void OnMqtt(esp_mqtt_event_handle_t event);
  static void MqttEvent(void *arg, esp_event_base_t base, int32_t id,
                        void *data);
};
