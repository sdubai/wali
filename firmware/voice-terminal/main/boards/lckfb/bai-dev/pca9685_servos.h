#pragma once
#include "servo_config.h"
#include <atomic>
#include <string>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mqtt_client.h>

// Board-lifetime object. The worker is the sole owner of PCA9685 writes and state.
class Pca9685Servos {
public:
    explicit Pca9685Servos(i2c_master_bus_handle_t bus);
    void RegisterTools();
    void StartNetwork();
private:
    friend struct ServoTestAccess;
    struct Command { int channel; int angle; }; // channel -1: disable all pulses
    i2c_master_bus_handle_t bus_;
    i2c_master_dev_handle_t device_ = nullptr;
    QueueHandle_t commands_ = nullptr;
    esp_mqtt_client_handle_t mqtt_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> refresh_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> worker_started_{false};
    int last_angle_[servo_config::kServoCount] = {-1, -1, -1, -1, -1};
    std::string client_id_;
    std::string status_topic_;
    std::string command_topics_[servo_config::kServoCount];
    std::string state_topics_[servo_config::kServoCount];
    bool Initialize();
    esp_err_t Write(uint8_t reg, uint8_t value);
    esp_err_t DisableOutputs();
    bool Submit(int channel, int angle);
    void Run();
    void Execute(const Command& command);
    void Publish(const std::string& topic, const std::string& payload, bool retain = true);
    void PublishState(int channel);
    void PublishDiscovery();
    void OnMqtt(esp_mqtt_event_handle_t event);
    static void MqttEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
};
