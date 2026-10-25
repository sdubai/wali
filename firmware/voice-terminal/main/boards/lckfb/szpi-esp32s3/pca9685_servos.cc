#include "pca9685_servos.h"
#include "servo_logic.h"
#include "mcp_server.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <freertos/task.h>
#include <cstdio>
#include <string_view>

namespace {
constexpr char kTag[] = "WalleServos";
}

Pca9685Servos::Pca9685Servos(i2c_master_bus_handle_t bus) : bus_(bus) {
    status_topic_ = std::string(servo_config::kPrefix) + "/status";
    for (int i = 0; i < 4; ++i) {
        const auto base = std::string(servo_config::kPrefix) + "/servo/" + std::to_string(i);
        command_topics_[i] = base + "/set";
        state_topics_[i] = base + "/state";
    }
    commands_ = xQueueCreate(8, sizeof(Command));
    if (!commands_) ESP_LOGE(kTag, "Cannot allocate servo command queue");
}

esp_err_t Pca9685Servos::Write(uint8_t reg, uint8_t value) {
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(device_, bytes, sizeof(bytes), 40);
}

esp_err_t Pca9685Servos::DisableOutputs() {
    // ALL_LED_OFF_H writes FULL_OFF to every channel (not a power switch).
    // A later per-channel write clears FULL_OFF only for that channel.
    return Write(0xfd, 0x10);
}

bool Pca9685Servos::Initialize() {
    if (i2c_master_probe(bus_, servo_config::kAddress, 40) != ESP_OK) return false;
    if (!device_) {
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = servo_config::kAddress;
        config.scl_speed_hz = 100000;
        if (i2c_master_bus_add_device(bus_, &config, &device_) != ESP_OK) return false;
    }
    // Address only 0x40. Never issue a general-call reset on the shared audio bus.
    if (DisableOutputs() != ESP_OK || Write(0x00, 0x30) != ESP_OK ||
        Write(0xfe, servo_config::kPrescale) != ESP_OK || Write(0x01, 0x04) != ESP_OK)
        return false;
    // Clear all channels to FULL_OFF; do not clear ALL_LED_OFF_H afterward.
    for (int channel = 0; channel < 16; ++channel) {
        const uint8_t bytes[] = {static_cast<uint8_t>(0x06 + 4 * channel), 0, 0, 0, 0x10};
        if (i2c_master_transmit(device_, bytes, sizeof(bytes), 40) != ESP_OK) return false;
    }
    if (Write(0x00, 0x20) != ESP_OK) return false; // AI on, SLEEP off, ALLCALL off
    vTaskDelay(pdMS_TO_TICKS(10));
    if (Write(0x00, 0xa0) != ESP_OK) return false; // RESTART
    for (int& angle : last_angle_) angle = -1;
    ESP_LOGI(kTag, "PCA9685 ready at 0x40; outputs off until a new command");
    return true;
}

bool Pca9685Servos::Submit(int channel, int angle) {
    if (!ready_ || !worker_started_ || !commands_) return false;
    if (channel != -1 && !servo_logic::Valid(channel, angle)) return false;
    Command command{channel, angle};
    if (channel == -1) {
        // Stop discards waiting moves, and goes before any later commands.
        xQueueReset(commands_);
        return xQueueSendToFront(commands_, &command, 0) == pdTRUE;
    }
    return xQueueSend(commands_, &command, 0) == pdTRUE;
}

void Pca9685Servos::RegisterTools() {
    auto& server = McpServer::GetInstance();
    server.AddTool("self.servos.set_angle",
        "按用户要求设置外接舵机的指令角度。servo是1到4，对应PCA9685通道0到3。"
        "测试范围60到120度。返回仅代表指令已排队，不代表舵机实际到位；不要自行发起运动。",
        PropertyList({Property("servo", kPropertyTypeInteger, 1, 4),
                      Property("angle", kPropertyTypeInteger,
                               servo_config::kMinAngle, servo_config::kMaxAngle)}),
        [this](const PropertyList& properties) -> ToolResult {
            if (!Submit(properties["servo"].value<int>() - 1, properties["angle"].value<int>()))
                return std::unexpected("舵机控制不可用或队列已满，请检查PCA9685接线和供电。");
            return ReturnValue(std::string("角度指令已排队；没有实际位置反馈。"));
        });
    server.AddTool("self.servos.stop_all",
        "停止四个舵机的控制脉冲并取消等待中的运动。不会切断舵机电源，也不保证机械急停。",
        PropertyList(), [this](const PropertyList&) -> ToolResult {
            if (!Submit(-1, 0)) return std::unexpected("舵机控制不可用，请断开外部电源停止。");
            return ReturnValue(std::string("停止脉冲指令已排队。"));
        });
}

void Pca9685Servos::StartNetwork() {
    if (worker_started_ || !commands_) return;
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char id[48];
    snprintf(id, sizeof(id), "xiaozhi-servos-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    client_id_ = id;
    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = servo_config::kBroker;
    config.credentials.client_id = client_id_.c_str();
    if (servo_config::kUsername[0]) {
        config.credentials.username = servo_config::kUsername;
        config.credentials.authentication.password = servo_config::kPassword;
    }
    config.session.keepalive = 30;
    config.session.last_will.topic = status_topic_.c_str();
    config.session.last_will.msg = "offline";
    config.session.last_will.qos = 1;
    config.session.last_will.retain = true;
    config.network.reconnect_timeout_ms = 5000;
    config.network.timeout_ms = 3000;
    mqtt_ = esp_mqtt_client_init(&config);
    if (mqtt_) {
        if (esp_mqtt_client_register_event(mqtt_, MQTT_EVENT_ANY, MqttEvent, this) != ESP_OK) {
            esp_mqtt_client_destroy(mqtt_);
            mqtt_ = nullptr;
        }
    }
    worker_started_ = true;
    if (xTaskCreate([](void* arg) { static_cast<Pca9685Servos*>(arg)->Run(); },
                    "walle_servos", 6144, this, 3, nullptr) != pdPASS) {
        worker_started_ = false;
        ESP_LOGE(kTag, "Cannot start servo worker");
        if (mqtt_) { esp_mqtt_client_destroy(mqtt_); mqtt_ = nullptr; }
        return;
    }
    if (mqtt_ && esp_mqtt_client_start(mqtt_) != ESP_OK)
        ESP_LOGE(kTag, "EMQX client did not start; voice control remains available");
}

void Pca9685Servos::Publish(const std::string& topic, const std::string& payload, bool retain) {
    if (!mqtt_ || !connected_) return;
    // Queue to the MQTT task instead of blocking the servo/audio workers on network IO.
    if (esp_mqtt_client_enqueue(mqtt_, topic.c_str(), payload.c_str(), payload.size(),
                                1, retain, true) < 0)
        ESP_LOGW(kTag, "MQTT publish queue rejected %s", topic.c_str());
}

void Pca9685Servos::PublishState(int channel) {
    Publish(state_topics_[channel], last_angle_[channel] < 0 ? "" : std::to_string(last_angle_[channel]));
}

void Pca9685Servos::PublishDiscovery() {
    for (int channel = 0; channel < 4; ++channel) {
        const auto suffix = "walle_esp32_01_servo_" + std::to_string(channel);
        cJSON* root = cJSON_CreateObject();
        if (!root) return;
        cJSON_AddStringToObject(root, "name", ("Walle Servo " + std::to_string(channel + 1)).c_str());
        cJSON_AddStringToObject(root, "unique_id", suffix.c_str());
        cJSON_AddStringToObject(root, "command_topic", command_topics_[channel].c_str());
        cJSON_AddStringToObject(root, "state_topic", state_topics_[channel].c_str());
        cJSON_AddStringToObject(root, "availability_topic", status_topic_.c_str());
        cJSON_AddStringToObject(root, "payload_available", "online");
        cJSON_AddStringToObject(root, "payload_not_available", "offline");
        cJSON_AddStringToObject(root, "mode", "slider");
        cJSON_AddStringToObject(root, "unit_of_measurement", "°");
        cJSON_AddNumberToObject(root, "min", servo_config::kMinAngle);
        cJSON_AddNumberToObject(root, "max", servo_config::kMaxAngle);
        cJSON_AddNumberToObject(root, "step", 1);
        cJSON_AddBoolToObject(root, "retain", false);
        cJSON* device = cJSON_AddObjectToObject(root, "device");
        if (device) {
            const char* ids[] = {"walle_esp32_01"};
            cJSON_AddItemToObject(device, "identifiers", cJSON_CreateStringArray(ids, 1));
            cJSON_AddStringToObject(device, "name", "WALL-E 舵机控制器");
            cJSON_AddStringToObject(device, "model", "LCKFB S3 + PCA9685");
        }
        char* json = cJSON_PrintUnformatted(root);
        if (json) {
            Publish("homeassistant/number/walle_esp32_01/servo_" + std::to_string(channel) + "/config", json);
            cJSON_free(json);
        }
        cJSON_Delete(root);
    }
}

void Pca9685Servos::MqttEvent(void* arg, esp_event_base_t, int32_t, void* data) {
    static_cast<Pca9685Servos*>(arg)->OnMqtt(static_cast<esp_mqtt_event_handle_t>(data));
}

void Pca9685Servos::OnMqtt(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        connected_ = true;
        for (const auto& topic : command_topics_)
            esp_mqtt_client_subscribe(mqtt_, topic.c_str(), 1);
        esp_mqtt_client_subscribe(mqtt_, "homeassistant/status", 1);
        refresh_ = true;
        ESP_LOGI(kTag, "Connected to company EMQX");
        break;
    case MQTT_EVENT_DISCONNECTED:
        connected_ = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(kTag, "EMQX error; check network, device credentials and topic ACLs");
        break;
    case MQTT_EVENT_DATA: {
        if (!event->topic || !event->data || event->current_data_offset != 0 ||
            event->total_data_len != event->data_len) return;
        const std::string_view topic(event->topic, event->topic_len);
        const std::string_view payload(event->data, event->data_len);
        if (topic == "homeassistant/status" && payload == "online") {
            refresh_ = true;
            return;
        }
        // Never replay retained motor commands after a reboot/reconnect.
        if (event->retain) return;
        int angle;
        if (!servo_logic::ParseAngle(payload, angle)) return;
        for (int i = 0; i < 4; ++i) {
            if (topic == command_topics_[i] && !Submit(i, angle))
                ESP_LOGW(kTag, "Rejected channel %d command: not ready or queue full", i);
        }
        break;
    }
    default: break;
    }
}

void Pca9685Servos::Run() {
    TickType_t last_probe = xTaskGetTickCount() - pdMS_TO_TICKS(5000);
    for (;;) {
        if (!ready_ && xTaskGetTickCount() - last_probe >= pdMS_TO_TICKS(5000)) {
            last_probe = xTaskGetTickCount();
            ready_ = Initialize();
            refresh_ = true;
            if (!ready_) ESP_LOGW(kTag, "PCA9685 not ready; voice/audio still available");
        }
        if (refresh_.exchange(false) && connected_) {
            PublishDiscovery();
            for (int i = 0; i < 4; ++i) PublishState(i);
            Publish(status_topic_, ready_ ? "online" : "offline");
        }
        Command command;
        if (xQueueReceive(commands_, &command, pdMS_TO_TICKS(50)) != pdTRUE) continue;
        if (!ready_) continue;
        Execute(command);
    }
}

void Pca9685Servos::Execute(const Command& command) {
    esp_err_t result;
    if (command.channel == -1) {
        result = DisableOutputs();
        for (int i = 0; i < 4; ++i) {
            last_angle_[i] = -1;
            PublishState(i);
        }
    } else {
        const uint16_t ticks = servo_logic::Ticks(command.channel, command.angle);
        const uint8_t data[] = {static_cast<uint8_t>(0x06 + 4 * command.channel),
                               0, 0, static_cast<uint8_t>(ticks & 0xff),
                               static_cast<uint8_t>(ticks >> 8)};
        result = i2c_master_transmit(device_, data, sizeof(data), 40);
        if (result == ESP_OK) {
            last_angle_[command.channel] = command.angle;
            PublishState(command.channel);
            ESP_LOGI(kTag, "Servo %d command %d deg written (no position feedback)",
                     command.channel + 1, command.angle);
        }
    }
    if (result != ESP_OK) {
        DisableOutputs(); // best effort; a broken bus cannot guarantee stop
        ready_ = false;
        xQueueReset(commands_);
        Publish(status_topic_, "offline");
        ESP_LOGE(kTag, "I2C write failed: %s", esp_err_to_name(result));
    }
}
