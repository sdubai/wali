#include "board_config.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

namespace {
constexpr char TAG[] = "WalleMotion";
i2c_master_bus_handle_t i2c_bus;
i2c_master_dev_handle_t pca9685;
int64_t drive_deadline_us;
EventGroupHandle_t wifi_events;
constexpr EventBits_t WIFI_CONNECTED = BIT0;
esp_mqtt_client_handle_t mqtt;

esp_err_t PcaWrite(uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(pca9685, data, sizeof(data), 50);
}

void InitPca9685() {
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = WALLE_I2C_PORT;
    bus.sda_io_num = WALLE_I2C_SDA;
    bus.scl_io_num = WALLE_I2C_SCL;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &i2c_bus));

    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = WALLE_PCA9685_ADDR;
    dev.scl_speed_hz = 100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev, &pca9685));

    ESP_ERROR_CHECK(PcaWrite(0x00, 0x30));       // sleep + auto increment
    ESP_ERROR_CHECK(PcaWrite(0xFE, 121));        // approximately 50 Hz
    ESP_ERROR_CHECK(PcaWrite(0x01, 0x04));
    ESP_ERROR_CHECK(PcaWrite(0x00, 0x20));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(PcaWrite(0x00, 0xA0));
    ESP_LOGI(TAG, "PCA9685 ready at 0x40");
}

bool SetServo(int id, int angle) {
    if (id < 1 || id > WALLE_SERVO_COUNT ||
        angle < WALLE_SERVO_MIN_DEG || angle > WALLE_SERVO_MAX_DEG) return false;

    // 500..2500 us in a 20 ms frame. Calibrate each servo before mechanical assembly.
    const int pulse_us = 500 + angle * 2000 / 180;
    const uint16_t ticks = static_cast<uint16_t>(pulse_us * 4096 / 20000);
    const uint8_t reg = static_cast<uint8_t>(0x06 + (id - 1) * 4);
    const uint8_t data[] = {reg, 0, 0,
                            static_cast<uint8_t>(ticks & 0xFF),
                            static_cast<uint8_t>(ticks >> 8)};
    return i2c_master_transmit(pca9685, data, sizeof(data), 50) == ESP_OK;
}

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

void SetOneMotor(int speed, gpio_num_t in_a, gpio_num_t in_b, ledc_channel_t channel) {
    speed = std::clamp(speed, -100, 100);
    gpio_set_level(in_a, speed > 0);
    gpio_set_level(in_b, speed < 0);
    const uint32_t duty = static_cast<uint32_t>(std::abs(speed) * 1023 / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

void Drive(int left, int right, int duration_ms) {
    SetOneMotor(left, WALLE_MOTOR_IN1, WALLE_MOTOR_IN2, LEDC_CHANNEL_0);
    SetOneMotor(right, WALLE_MOTOR_IN3, WALLE_MOTOR_IN4, LEDC_CHANNEL_1);
    const int timeout = std::clamp(duration_ms, 1, WALLE_DRIVE_TIMEOUT_MS);
    drive_deadline_us = esp_timer_get_time() + timeout * 1000LL;
}

void Stop() {
    drive_deadline_us = 0;
    SetOneMotor(0, WALLE_MOTOR_IN1, WALLE_MOTOR_IN2, LEDC_CHANNEL_0);
    SetOneMotor(0, WALLE_MOTOR_IN3, WALLE_MOTOR_IN4, LEDC_CHANNEL_1);
}

void ExecuteLine(char* line) {
    cJSON* root = cJSON_Parse(line);
    if (!root) return;
    const cJSON* cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return;
    }
    if (!strcmp(cmd->valuestring, "drive")) {
        const cJSON* left = cJSON_GetObjectItem(root, "left");
        const cJSON* right = cJSON_GetObjectItem(root, "right");
        const cJSON* duration = cJSON_GetObjectItem(root, "duration");
        if (cJSON_IsNumber(left) && cJSON_IsNumber(right))
            Drive(left->valueint, right->valueint,
                  cJSON_IsNumber(duration) ? duration->valueint : WALLE_DRIVE_TIMEOUT_MS);
    } else if (!strcmp(cmd->valuestring, "servo")) {
        const cJSON* id = cJSON_GetObjectItem(root, "id");
        const cJSON* angle = cJSON_GetObjectItem(root, "angle");
        if (cJSON_IsNumber(id) && cJSON_IsNumber(angle) && !SetServo(id->valueint, angle->valueint))
            ESP_LOGW(TAG, "Rejected servo command");
    } else if (!strcmp(cmd->valuestring, "stop")) {
        Stop();
    }
    cJSON_Delete(root);
}

void MqttEvent(void*, esp_event_base_t, int32_t event_id, void* event_data) {
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (event_id == MQTT_EVENT_CONNECTED) {
        esp_mqtt_client_subscribe(mqtt, "walle/motion/drive/set", 1);
        esp_mqtt_client_subscribe(mqtt, "walle/motion/servo/set", 1);
        esp_mqtt_client_subscribe(mqtt, "walle/motion/stop", 1);
        esp_mqtt_client_publish(mqtt, "walle/motion/status", "online", 0, 1, true);
        ESP_LOGI(TAG, "MQTT connected");
    } else if (event_id == MQTT_EVENT_DATA) {
        if (event->current_data_offset != 0 || event->data_len != event->total_data_len) return;
        const std::string topic(event->topic, event->topic_len);
        if (topic == "walle/motion/stop") {
            Stop();
            return;
        }
        if (event->data_len <= 0 || event->data_len >= 256) return;
        char line[256];
        memcpy(line, event->data, event->data_len);
        line[event->data_len] = 0;
        ExecuteLine(line);
    }
}

void WifiEvent(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED);
    }
}

void NetworkTask(void*) {
    xEventGroupWaitBits(wifi_events, WIFI_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);
    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = CONFIG_WALLE_MQTT_URI;
    if (CONFIG_WALLE_MQTT_USERNAME[0]) config.credentials.username = CONFIG_WALLE_MQTT_USERNAME;
    if (CONFIG_WALLE_MQTT_PASSWORD[0])
        config.credentials.authentication.password = CONFIG_WALLE_MQTT_PASSWORD;
    config.session.last_will.topic = "walle/motion/status";
    config.session.last_will.msg = "offline";
    config.session.last_will.qos = 1;
    config.session.last_will.retain = true;
    mqtt = esp_mqtt_client_init(&config);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, MqttEvent, nullptr));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt));
    vTaskDelete(nullptr);
}

void InitNetwork() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, WifiEvent, nullptr));
    wifi_config_t wifi = {};
    strlcpy(reinterpret_cast<char*>(wifi.sta.ssid), CONFIG_WALLE_WIFI_SSID, sizeof(wifi.sta.ssid));
    strlcpy(reinterpret_cast<char*>(wifi.sta.password), CONFIG_WALLE_WIFI_PASSWORD, sizeof(wifi.sta.password));
    wifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    xTaskCreate(NetworkTask, "motion_network", 6144, nullptr, 3, nullptr);
}

void UartTask(void*) {
    char line[256];
    size_t used = 0;
    uint8_t byte;
    for (;;) {
        if (uart_read_bytes(WALLE_UART_PORT, &byte, 1, pdMS_TO_TICKS(20)) == 1) {
            if (byte == '\n') {
                line[used] = 0;
                ExecuteLine(line);
                used = 0;
            } else if (byte != '\r' && used + 1 < sizeof(line)) {
                line[used++] = static_cast<char>(byte);
            } else if (used + 1 >= sizeof(line)) {
                used = 0;
            }
        }
        if (drive_deadline_us && esp_timer_get_time() >= drive_deadline_us) Stop();
    }
}
} // namespace

extern "C" void app_main() {
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs);
    }
    InitMotors();
    Stop();
    InitPca9685();

    uart_config_t uart = {};
    uart.baud_rate = WALLE_UART_BAUD;
    uart.data_bits = UART_DATA_8_BITS;
    uart.parity = UART_PARITY_DISABLE;
    uart.stop_bits = UART_STOP_BITS_1;
    uart.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(WALLE_UART_PORT, 2048, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(WALLE_UART_PORT, &uart));
    ESP_ERROR_CHECK(uart_set_pin(WALLE_UART_PORT, WALLE_UART_TX, WALLE_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(UartTask, "motion_uart", 6144, nullptr, 5, nullptr);
    if (CONFIG_WALLE_WIFI_SSID[0]) InitNetwork();
    ESP_LOGI(TAG, "Motion controller ready");
}

