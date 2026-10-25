#include "motion_link.h"

#include "config.h"
#include "mcp_server.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/task.h>

namespace {
constexpr char kTag[] = "MotionLink";
constexpr int kServoCount = 7;
constexpr int kMinimumAngle = 10;
constexpr int kMaximumAngle = 170;
constexpr int kMinimumSpeed = 20;
constexpr int kMaximumSpeed = 100;
constexpr int kMaximumDriveTimeMs = 5000;
constexpr int kRefreshIntervalMs = 250;
}  // namespace

MotionLink::MotionLink() {
    queue_ = xQueueCreate(8, sizeof(Command));
    if (queue_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create command queue");
        return;
    }

    uart_config_t uart_config = {};
    uart_config.baud_rate = MOTION_UART_BAUD_RATE;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(MOTION_UART_PORT, 1024, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(MOTION_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(MOTION_UART_PORT, MOTION_UART_TX_PIN, MOTION_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    if (xTaskCreate([](void* context) { static_cast<MotionLink*>(context)->Run(); }, "motion_link",
                    4096, this, 4, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Failed to start UART worker");
        vQueueDelete(queue_);
        queue_ = nullptr;
        return;
    }
    ESP_LOGI(kTag, "UART ready: TX=%d RX=%d baud=%d", MOTION_UART_TX_PIN, MOTION_UART_RX_PIN,
             MOTION_UART_BAUD_RATE);
}

bool MotionLink::Submit(const Command& command, bool urgent) {
    if (queue_ == nullptr) {
        return false;
    }
    if (urgent) {
        xQueueReset(queue_);
        return xQueueSendToFront(queue_, &command, 0) == pdTRUE;
    }
    return xQueueSend(queue_, &command, 0) == pdTRUE;
}

void MotionLink::RegisterTools() {
    auto& server = McpServer::GetInstance();

    server.AddTool(
        "self.robot.drive",
        "控制瓦力底盘移动。left和right分别是左右履带速度，范围-100到100；正数前进，"
        "负数后退。前进时两边同为正，后退时同为负，左转时左负右正，右转时左正右负。"
        "duration_ms是持续时间，范围100到5000毫秒。只在用户明确要求移动时调用。",
        PropertyList({Property("left", kPropertyTypeInteger, -kMaximumSpeed, kMaximumSpeed),
                      Property("right", kPropertyTypeInteger, -kMaximumSpeed, kMaximumSpeed),
                      Property("duration_ms", kPropertyTypeInteger, 100, kMaximumDriveTimeMs)}),
        [this](const PropertyList& properties) -> ToolResult {
            const int left = properties["left"].value<int>();
            const int right = properties["right"].value<int>();
            if ((left != 0 && std::abs(left) < kMinimumSpeed) ||
                (right != 0 && std::abs(right) < kMinimumSpeed)) {
                return std::unexpected("非零履带速度必须至少为20。请提高速度或停止。 ");
            }
            Command command{CommandType::kDrive, left, right,
                            properties["duration_ms"].value<int>()};
            if (!Submit(command)) {
                return std::unexpected("运动控制队列已满或UART不可用。 ");
            }
            return ReturnValue(std::string("底盘移动指令已发送。 "));
        });

    server.AddTool("self.robot.stop",
                   "立即停止瓦力左右履带。用户说停止、别动、危险或取消移动时调用。", PropertyList(),
                   [this](const PropertyList&) -> ToolResult {
                       if (!Submit(Command{CommandType::kStop, 0, 0, 0}, true)) {
                           return std::unexpected("停止指令发送失败，请立即切断电机电源。 ");
                       }
                       return ReturnValue(std::string("停止指令已发送。 "));
                   });

    server.AddTool(
        "self.servos.set_angle",
        "设置瓦力某个舵机的目标角度。servo为1到7：1左臂、2右臂、3左脖子抬升、"
        "4右脖子抬升、5脖子旋转、6左眼升降、7右眼升降。angle范围10到170度。"
        "只有用户明确要求动作时才调用。",
        PropertyList({Property("servo", kPropertyTypeInteger, 1, kServoCount),
                      Property("angle", kPropertyTypeInteger, kMinimumAngle, kMaximumAngle)}),
        [this](const PropertyList& properties) -> ToolResult {
            Command command{CommandType::kServo, properties["servo"].value<int>(),
                            properties["angle"].value<int>(), 0};
            if (!Submit(command)) {
                return std::unexpected("舵机指令队列已满或UART不可用。 ");
            }
            return ReturnValue(std::string("舵机角度指令已发送；当前没有位置反馈。 "));
        });
}

void MotionLink::WriteLine(const char* line) {
    const size_t length = strlen(line);
    if (uart_write_bytes(MOTION_UART_PORT, line, length) != static_cast<int>(length)) {
        ESP_LOGE(kTag, "UART write failed");
    }
}

void MotionLink::SendDrive(int left, int right) {
    char json[112];
    snprintf(json, sizeof(json), "{\"cmd\":\"drive\",\"left\":%d,\"right\":%d,\"duration\":700}\n",
             left, right);
    WriteLine(json);
}

void MotionLink::SendServo(int servo, int angle) {
    char json[80];
    snprintf(json, sizeof(json), "{\"cmd\":\"servo\",\"id\":%d,\"angle\":%d}\n", servo, angle);
    WriteLine(json);
}

void MotionLink::SendStop() { WriteLine("{\"cmd\":\"stop\"}\n"); }

void MotionLink::Run() {
    Command command;
    for (;;) {
        if (xQueueReceive(queue_, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (command.type == CommandType::kStop) {
            SendStop();
            continue;
        }
        if (command.type == CommandType::kServo) {
            SendServo(command.first, command.second);
            continue;
        }

        int remaining_ms = command.duration_ms;
        bool interrupted = false;
        while (remaining_ms > 0) {
            SendDrive(command.first, command.second);
            const int wait_ms = std::min(remaining_ms, kRefreshIntervalMs);
            Command next;
            if (xQueueReceive(queue_, &next, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
                SendStop();
                command = next;
                interrupted = true;
                break;
            }
            remaining_ms -= wait_ms;
        }
        if (!interrupted) {
            SendStop();
        } else if (command.type == CommandType::kStop) {
            SendStop();
        } else if (command.type == CommandType::kServo) {
            SendServo(command.first, command.second);
        } else {
            // Requeue the replacement drive command so it follows the same watchdog refresh path.
            xQueueSendToFront(queue_, &command, 0);
        }
    }
}
