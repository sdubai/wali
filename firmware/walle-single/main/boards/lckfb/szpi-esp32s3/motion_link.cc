#include "motion_link.h"
#include "drive_lease.h"

#include "cJSON.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include <driver/uart.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/task.h>

namespace {
constexpr char kTag[] = "MotionLink";
constexpr int kServoCount = 12;
constexpr int kMinimumAngle = 10;
constexpr int kMaximumAngle = 170;
constexpr int kMinimumSpeed = 20;
constexpr int kMaximumSpeed = 100;
constexpr int kDefaultDriveTimeMs = walle::kDefaultVoiceDriveMs;
constexpr int kMaximumDriveTimeMs = walle::kMaximumVoiceDriveMs;
}  // namespace

MotionLink* MotionLink::instance_ = nullptr;

MotionLink::MotionLink() {
    instance_ = this;
    // Motion commands are state, not a job list. Keeping more than the newest
    // command makes an old direction run after the operator has released it.
    queue_ = xQueueCreate(1, sizeof(Command));
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
    if (xTaskCreate([](void* context) { static_cast<MotionLink*>(context)->ReceiveTelemetry(); },
                    "motion_rx", 4096, this, 3, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "Failed to start distance telemetry receiver");
    }
}

bool MotionLink::TriggerIntroductionFromSpeech() {
    if (instance_ == nullptr) {
        ESP_LOGW(kTag, "Cannot start introduction: MotionLink is not initialized");
        return false;
    }
    instance_->introduction_pending_until_us_ = 0;
    ESP_LOGI(kTag, "Starting introduction action");
    return instance_->Submit(Command{CommandType::kAction, 0, 0, 0});
}

void MotionLink::ArmIntroductionFromSpeech() {
    if (instance_ == nullptr)
        return;
    // The local server can take several seconds to start its reply. Wait for
    // the matching TTS start event so speech and movement begin together.
    instance_->introduction_pending_until_us_ = esp_timer_get_time() + 60000000LL;
    ESP_LOGI(kTag, "Introduction recognized; waiting for TTS start");
}

bool MotionLink::StartPendingIntroductionWithTts() {
    if (instance_ == nullptr || instance_->introduction_pending_until_us_ == 0)
        return false;
    const bool valid = esp_timer_get_time() <= instance_->introduction_pending_until_us_;
    instance_->introduction_pending_until_us_ = 0;
    if (!valid) {
        ESP_LOGW(kTag, "Pending introduction expired before TTS started");
        return false;
    }
    ESP_LOGI(kTag, "TTS started; launching synchronized introduction action");
    return instance_->Submit(Command{CommandType::kAction, 0, 0, 0});
}

bool MotionLink::Submit(const Command& command, bool urgent) {
    if (queue_ == nullptr) {
        return false;
    }
    // Latch urgent requests independently: a concurrent servo update must not
    // overwrite a queued stop and allow the drive heartbeat to continue.
    if (urgent && command.type == CommandType::kStop)
        stop_pending_.store(true);
    if (urgent && command.type == CommandType::kCancelDrive)
        cancel_drive_pending_.store(true);
    return xQueueOverwrite(queue_, &command) == pdTRUE;
}

void MotionLink::RegisterTools() {
    auto& server = McpServer::GetInstance();

    server.AddTool(
        "self.robot.drive",
        "控制瓦力底盘移动。left和right分别是左右履带速度，范围-100到100；正数前进，"
        "负数后退。前进时两边同为正，后退时同为负，左转时左负右正，右转时左正右负。"
        "用户未指定速度时用100%：前进100/100、后退-100/-100、左转-100/100、右转100/-100；"
        "用户明确要求慢速或指定速度时才降低。duration_ms范围100到60000毫秒，"
        "未指定时长默认10000毫秒（10秒），例如前进20秒用20000。只在用户明确要求移动时调用。",
        PropertyList(
            {Property("left", kPropertyTypeInteger, kMaximumSpeed, -kMaximumSpeed, kMaximumSpeed),
             Property("right", kPropertyTypeInteger, kMaximumSpeed, -kMaximumSpeed, kMaximumSpeed),
             Property("duration_ms", kPropertyTypeInteger, kDefaultDriveTimeMs, 100,
                      kMaximumDriveTimeMs)}),
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
            ESP_LOGI(kTag, "Voice drive: left=%d%% right=%d%% duration=%dms", left, right,
                     command.duration_ms);
            return ReturnValue(std::string("底盘移动指令已发送，左履带") + std::to_string(left) +
                               "%、右履带" + std::to_string(right) + "%、持续" +
                               std::to_string(command.duration_ms) + "毫秒。");
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
        "设置瓦力某个舵机的目标角度。servo 3是右眼，安全范围30到60度，30最低、"
        "60最高；servo 4是左眼，安全范围110到140度，140最低、110最高。"
        "左右眼因镜像安装而方向相反。servo 5控制脖子左右旋转，10度最右、108度正前方、"
        "170度最左。servo 6和servo 7共同控制头部俯仰：平视为6号145度、7号70度；"
        "俯视时保持7号70度，只将6号从145度向115度调整；仰视时保持6号145度，只将"
        "7号从70度向150度调整。运动板具有互锁保护。"
        "其他舵机angle范围10到170度。"
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

    server.AddTool("self.robot.introduce",
                   "当用户说‘请介绍下自己’、‘介绍一下你自己’或意思相同的话时必须调用。"
                   "该工具会启动底盘转身、双臂、头部和双眼的完整自我介绍动作，并与回答同时进行。"
                   "调用后不要等待动作结束，也不要再调用底盘或单个舵机工具，"
                   "并只用下面的固定话术回答："
                   "大家好，我是瓦力，是老白亲手打造的机器人！"
                   "我会卖萌、会跳舞，还能陪你聊天、帮你认东西。上知天文地理，下知生活趣事，"
                   "偶尔也会犯点小迷糊。当然，卖萌只是副业，守护地球才是我的主业！"
                   "很高兴认识你，要和我交个朋友吗？",
                   PropertyList(), [this](const PropertyList&) -> ToolResult {
                       ArmIntroductionFromSpeech();
                       return ReturnValue(std::string(
                           "自我介绍动作已准备，将在语音开始播放时同步启动。请立即按工具描述中的"
                           "固定话术介绍自己。"));
                   });
}

void MotionLink::StartNetwork() {
    if (mqtt_ != nullptr)
        return;
    mqtt_uri_ = MOTION_MQTT_URI;
    mqtt_username_ = MOTION_MQTT_USERNAME;
    mqtt_password_ = MOTION_MQTT_PASSWORD;
    {
        Settings settings("motion_mqtt");
        cJSON* saved = cJSON_Parse(settings.GetString("config").c_str());
        const auto* uri = cJSON_GetObjectItemCaseSensitive(saved, "uri");
        const auto* username = cJSON_GetObjectItemCaseSensitive(saved, "username");
        const auto* password = cJSON_GetObjectItemCaseSensitive(saved, "password");
        if (cJSON_IsString(uri) && cJSON_IsString(username) && cJSON_IsString(password)) {
            mqtt_uri_ = uri->valuestring;
            mqtt_username_ = username->valuestring;
            mqtt_password_ = password->valuestring;
        }
        cJSON_Delete(saved);
    }
    ConnectMqtt();
    StartConfigServer();
}

esp_err_t MotionLink::ConfigPage(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(
        request,
        R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>瓦力 MQTT 配置</title>
<style>body{max-width:520px;margin:40px auto;padding:16px;font:18px sans-serif}input,button{box-sizing:border-box;width:100%;padding:12px;margin:8px 0 18px}label{display:block}</style>
<h2>瓦力运动 MQTT 配置</h2><p>保存到语音板，重启后仍有效。请在同一 Wi-Fi 下操作。</p>
<form id="config"><label>设备当前 MQTT 密码（用于验证）<input name="current" type="password" required autocomplete="off"></label>
<label>新 MQTT 地址<input name="uri" value="wss://walle.kangqing.net:443/mqtt" required></label>
<label>新 MQTT 用户名<input name="username" required autocomplete="off"></label>
<label>新 MQTT 密码<input name="password" type="password" required autocomplete="new-password"></label>
<button>保存并重新连接</button></form><p id="result"></p><p>此处只修改语音板连接信息，EMQX 用户密码和控制网页需同步修改。</p>
<script>document.querySelector('form').onsubmit=async e=>{e.preventDefault();const b=e.target.querySelector('button');b.disabled=true;try{const r=await fetch('/mqtt-config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(Object.fromEntries(new FormData(e.target)))});document.querySelector('#result').textContent=await r.text();if(r.ok){e.target.current.value=e.target.password.value;e.target.password.value=''}}catch(x){document.querySelector('#result').textContent='请求失败，请检查设备连接'}finally{b.disabled=false}};</script></html>)HTML");
}

esp_err_t MotionLink::SaveConfig(httpd_req_t* request) {
    auto* self = static_cast<MotionLink*>(request->user_ctx);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    if (request->content_len == 0 || request->content_len > 2048)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid body size");
    std::string body(request->content_len, '\0');
    size_t received = 0;
    while (received < body.size()) {
        const int size = httpd_req_recv(request, body.data() + received, body.size() - received);
        if (size <= 0)
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete request");
        received += size;
    }
    cJSON* root = cJSON_ParseWithLength(body.data(), body.size());
    const auto* current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const auto* uri = cJSON_GetObjectItemCaseSensitive(root, "uri");
    const auto* username = cJSON_GetObjectItemCaseSensitive(root, "username");
    const auto* password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(current) || !cJSON_IsString(uri) || !cJSON_IsString(username) ||
        !cJSON_IsString(password)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing configuration fields");
    }
    if (self->mqtt_password_ != current->valuestring) {
        cJSON_Delete(root);
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request, "当前设备密码不正确，配置未修改。");
    }
    const std::string next_uri = uri->valuestring;
    const std::string next_username = username->valuestring;
    const std::string next_password = password->valuestring;
    if ((next_uri.rfind("wss://", 0) != 0 && next_uri.rfind("mqtts://", 0) != 0) ||
        next_uri.size() > 256 || next_uri.find('@') != std::string::npos || next_username.empty() ||
        next_username.size() > 128 || next_password.empty() || next_password.size() > 256) {
        cJSON_Delete(root);
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST,
            "Use wss:// or mqtts:// and nonempty credentials within size limits");
    }
    cJSON_DeleteItemFromObjectCaseSensitive(root, "current");
    char* saved = cJSON_PrintUnformatted(root);
    nvs_handle_t handle = 0;
    esp_err_t err = saved ? nvs_open("motion_mqtt", NVS_READWRITE, &handle) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "config", saved);
        if (err == ESP_OK)
            err = nvs_commit(handle);
        nvs_close(handle);
    }
    cJSON_free(saved);
    cJSON_Delete(root);
    if (err != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "NVS save failed; retry");
    // The HTTP task owns reconfiguration; never destroy MQTT from its event callback.
    std::lock_guard<std::mutex> mqtt_lock(self->mqtt_mutex_);
    self->mqtt_connected_.store(false);
    if (self->mqtt_) {
        esp_mqtt_client_stop(self->mqtt_);
        esp_mqtt_client_destroy(self->mqtt_);
        self->mqtt_ = nullptr;
    }
    self->mqtt_uri_ = next_uri;
    self->mqtt_username_ = next_username;
    self->mqtt_password_ = next_password;
    self->ConnectMqtt();
    return httpd_resp_sendstr(request, "已保存到 NVS，正在重新连接。请查看串口连接结果。");
}

void MotionLink::StartConfigServer() {
    if (config_server_)
        return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 82;
    config.ctrl_port = 32771;
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    if (httpd_start(&config_server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start MQTT configuration page");
        return;
    }
    const httpd_uri_t page = {
        .uri = "/", .method = HTTP_GET, .handler = ConfigPage, .user_ctx = this};
    const httpd_uri_t save = {
        .uri = "/mqtt-config", .method = HTTP_POST, .handler = SaveConfig, .user_ctx = this};
    if (httpd_register_uri_handler(config_server_, &page) != ESP_OK ||
        httpd_register_uri_handler(config_server_, &save) != ESP_OK) {
        httpd_stop(config_server_);
        config_server_ = nullptr;
        ESP_LOGE(kTag, "Failed to register MQTT configuration handlers");
        return;
    }
    ESP_LOGI(kTag, "MQTT configuration page started on port 82");
}

void MotionLink::ConnectMqtt() {
    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = mqtt_uri_.c_str();
    config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    if (!mqtt_username_.empty())
        config.credentials.username = mqtt_username_.c_str();
    if (!mqtt_password_.empty())
        config.credentials.authentication.password = mqtt_password_.c_str();
    config.session.keepalive = 30;
    config.session.last_will.topic = "walle/motion/status";
    config.session.last_will.msg = "offline";
    config.session.last_will.qos = 1;
    config.session.last_will.retain = true;
    config.network.reconnect_timeout_ms = 5000;
    config.network.timeout_ms = 3000;
    mqtt_ = esp_mqtt_client_init(&config);
    if (mqtt_ == nullptr ||
        esp_mqtt_client_register_event(mqtt_, MQTT_EVENT_ANY, MqttEvent, this) != ESP_OK ||
        esp_mqtt_client_start(mqtt_) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start webpage MQTT relay");
        if (mqtt_) {
            esp_mqtt_client_destroy(mqtt_);
            mqtt_ = nullptr;
        }
    }
}

void MotionLink::MqttEvent(void* arg, esp_event_base_t, int32_t, void* data) {
    static_cast<MotionLink*>(arg)->OnMqtt(static_cast<esp_mqtt_event_handle_t>(data));
}

void MotionLink::OnMqtt(esp_mqtt_event_handle_t event) {
    if (event->event_id == MQTT_EVENT_CONNECTED) {
        mqtt_connected_.store(true);
        esp_mqtt_client_subscribe(mqtt_, "walle/motion/drive/set", 1);
        esp_mqtt_client_subscribe(mqtt_, "walle/motion/servo/set", 1);
        esp_mqtt_client_subscribe(mqtt_, "walle/motion/stop", 1);
        esp_mqtt_client_publish(mqtt_, "walle/motion/status", "online", 0, 1, true);

        esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info = {};
        if (station != nullptr && esp_netif_get_ip_info(station, &ip_info) == ESP_OK &&
            ip_info.ip.addr != 0) {
            // The continuous camera preview is temporarily disabled so voice
            // recognition and TTS have exclusive use of the Wi-Fi link.
            esp_mqtt_client_publish(mqtt_, "walle/camera/url", "", 0, 1, true);
        }
        ESP_LOGI(kTag, "Webpage MQTT relay connected");
        return;
    }
    if (event->event_id == MQTT_EVENT_DISCONNECTED) {
        mqtt_connected_.store(false);
        return;
    }
    if (event->event_id != MQTT_EVENT_DATA || event->current_data_offset != 0 ||
        event->data_len != event->total_data_len || event->retain) {
        return;
    }

    const std::string_view topic(event->topic, event->topic_len);
    if (topic == "walle/motion/stop") {
        Submit(Command{CommandType::kStop, 0, 0, 0}, true);
        return;
    }
    if (event->data_len <= 0 || event->data_len >= 256)
        return;
    char payload[256];
    memcpy(payload, event->data, event->data_len);
    payload[event->data_len] = 0;
    cJSON* root = cJSON_Parse(payload);
    if (!root)
        return;

    if (topic == "walle/motion/drive/set") {
        const cJSON* left = cJSON_GetObjectItemCaseSensitive(root, "left");
        const cJSON* right = cJSON_GetObjectItemCaseSensitive(root, "right");
        const cJSON* duration = cJSON_GetObjectItemCaseSensitive(root, "duration");
        if (cJSON_IsNumber(left) && cJSON_IsNumber(right)) {
            const int l = std::clamp(left->valueint, -kMaximumSpeed, kMaximumSpeed);
            const int r = std::clamp(right->valueint, -kMaximumSpeed, kMaximumSpeed);
            const int ms = cJSON_IsNumber(duration)
                               ? std::clamp(duration->valueint, 100, kMaximumDriveTimeMs)
                               : 500;
            Submit(Command{CommandType::kDrive, l, r, ms});
        }
    } else if (topic == "walle/motion/servo/set") {
        const cJSON* servo = cJSON_GetObjectItemCaseSensitive(root, "id");
        const cJSON* angle = cJSON_GetObjectItemCaseSensitive(root, "angle");
        if (cJSON_IsNumber(servo) && cJSON_IsNumber(angle) && servo->valueint >= 1 &&
            servo->valueint <= kServoCount && angle->valueint >= kMinimumAngle &&
            angle->valueint <= kMaximumAngle) {
            ESP_LOGI(kTag, "MQTT servo command: id=%d angle=%d", servo->valueint, angle->valueint);
            Submit(Command{CommandType::kServo, servo->valueint, angle->valueint, 0});
        }
    }
    cJSON_Delete(root);
}

void MotionLink::WriteLine(const char* line) {
    const size_t length = strlen(line);
    if (uart_write_bytes(MOTION_UART_PORT, line, length) != static_cast<int>(length)) {
        ESP_LOGE(kTag, "UART write failed");
    }
}

void MotionLink::SendDrive(int left, int right, int duration_ms) {
    char json[112];
    snprintf(json, sizeof(json), "{\"cmd\":\"drive\",\"left\":%d,\"right\":%d,\"duration\":%d}\n",
             left, right, duration_ms);
    WriteLine(json);
}

void MotionLink::SendServo(int servo, int angle) {
    char json[80];
    snprintf(json, sizeof(json), "{\"cmd\":\"servo\",\"id\":%d,\"angle\":%d}\n", servo, angle);
    ESP_LOGI(kTag, "UART servo command: id=%d angle=%d", servo, angle);
    WriteLine(json);
}

void MotionLink::SendAction(const char* name) {
    char json[80];
    snprintf(json, sizeof(json), "{\"cmd\":\"action\",\"name\":\"%s\"}\n", name);
    // The motion board may still be finishing UART/PCA9685 startup on the first
    // conversation after boot. Send the idempotent action request three times;
    // the motion controller ignores duplicates while an action is running.
    for (int attempt = 1; attempt <= 3; ++attempt) {
        ESP_LOGI(kTag, "UART action command: %s (attempt %d/3)", name, attempt);
        WriteLine(json);
        if (attempt < 3)
            vTaskDelay(pdMS_TO_TICKS(120));
    }
}

void MotionLink::SendStop() { WriteLine("{\"cmd\":\"stop\"}\n"); }

void MotionLink::ReceiveTelemetry() {
    char line[256];
    size_t used = 0;
    bool overflow = false;
    uint8_t bytes[128];
    for (;;) {
        const int count =
            uart_read_bytes(MOTION_UART_PORT, bytes, sizeof(bytes), pdMS_TO_TICKS(50));
        for (int i = 0; i < count; ++i) {
            const char byte = static_cast<char>(bytes[i]);
            if (byte == '\n') {
                if (!overflow && used) {
                    line[used] = 0;
                    ForwardTelemetry(line);
                }
                used = 0;
                overflow = false;
            } else if (byte != '\r' && !overflow) {
                if (used + 1 < sizeof(line) && byte != '\0')
                    line[used++] = byte;
                else
                    overflow = true;
            }
        }
    }
}

void MotionLink::ForwardTelemetry(const char* line) {
    cJSON* root = cJSON_Parse(line);
    if (!root)
        return;
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "motion_override") == 0) {
        // The PS2 driver now owns the tracks. Do not resume an old voice move
        // after the operator releases the stick, and do not stop the PS2 move.
        Submit(Command{CommandType::kCancelDrive, 0, 0, 0}, true);
        cJSON_Delete(root);
        return;
    }
    if (cJSON_IsString(type) && strcmp(type->valuestring, "wifi_sync_request") == 0) {
        const auto* id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
        if (cJSON_IsString(id))
            SendPiWifiConfig(id->valuestring);
        cJSON_Delete(root);
        return;
    }
    const cJSON* valid = cJSON_GetObjectItemCaseSensitive(root, "valid");
    const cJSON* cm = cJSON_GetObjectItemCaseSensitive(root, "distance_cm");
    char payload[128] = {};
    if (cJSON_IsString(type) && strcmp(type->valuestring, "distance") == 0 && cJSON_IsBool(valid)) {
        if (cJSON_IsTrue(valid) && cJSON_IsNumber(cm) && std::isfinite(cm->valuedouble) &&
            cm->valuedouble >= 2.0 && cm->valuedouble <= 400.0) {
            snprintf(payload, sizeof(payload), "{\"valid\":true,\"distance_cm\":%.1f}",
                     cm->valuedouble);
        } else if (cJSON_IsFalse(valid) && cJSON_IsNull(cm)) {
            snprintf(payload, sizeof(payload),
                     "{\"valid\":false,\"distance_cm\":null,\"reason\":\"no_echo\"}");
        }
    }
    cJSON_Delete(root);
    if (!payload[0])
        return;
    // Keep only live readings; never accumulate telemetry while disconnected.
    std::lock_guard<std::mutex> lock(mqtt_mutex_);
    if (mqtt_ && mqtt_connected_.load()) {
        esp_mqtt_client_enqueue(mqtt_, "walle/motion/distance", payload, 0, 0, false, true);
    }
}

void MotionLink::SendPiWifiConfig(const char* request_id) {
    if (strlen(request_id) != 16 || strspn(request_id, "0123456789abcdef") != 16)
        return;
    static int64_t last_request_us = -10000000;
    const int64_t now = esp_timer_get_time();
    if (now - last_request_us < 10000000)
        return;
    last_request_us = now;
    wifi_ap_record_t ap = {};
    wifi_config_t config = {};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
        esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK)
        return;
    if (memcmp(ap.ssid, config.sta.ssid, sizeof(config.sta.ssid)) != 0)
        return;
    // Only export the currently connected personal/open Wi-Fi, not enterprise credentials.
    if (ap.authmode != WIFI_AUTH_OPEN && ap.authmode != WIFI_AUTH_WPA_PSK &&
        ap.authmode != WIFI_AUTH_WPA2_PSK && ap.authmode != WIFI_AUTH_WPA_WPA2_PSK &&
        ap.authmode != WIFI_AUTH_WPA3_PSK && ap.authmode != WIFI_AUTH_WPA2_WPA3_PSK)
        return;
    char ssid[33] = {}, password[65] = {};
    memcpy(ssid, config.sta.ssid, 32);
    memcpy(password, config.sta.password, 64);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "pi_wifi_config");
    cJSON_AddStringToObject(root, "request_id", request_id);
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddStringToObject(root, "password", ap.authmode == WIFI_AUTH_OPEN ? "" : password);
    cJSON_AddStringToObject(root, "security",
                            ap.authmode == WIFI_AUTH_OPEN
                                ? "open"
                                : (ap.authmode == WIFI_AUTH_WPA3_PSK ? "sae" : "wpa-psk"));
    char* json = cJSON_PrintUnformatted(root);
    if (json) {
        const std::string packet = std::string(json) + "\n";
        if (packet.size() < 1024)
            WriteLine(packet.c_str());
        memset(json, 0, strlen(json));
        cJSON_free(json);
    }
    cJSON_Delete(root);
    memset(password, 0, sizeof(password));
    memset(&config, 0, sizeof(config));
}

void MotionLink::Run() {
    Command command;
    Command driving{CommandType::kDrive, 0, 0, 0};
    walle::DriveLease lease;
    for (;;) {
        const TickType_t wait = lease.Active() ? pdMS_TO_TICKS(20) : portMAX_DELAY;
        const bool received = xQueueReceive(queue_, &command, wait) == pdTRUE;
        const bool stop = stop_pending_.exchange(false);
        const bool cancel = cancel_drive_pending_.exchange(false);
        if (stop || cancel) {
            lease.Cancel();
            if (stop)
                SendStop();
            continue;
        }
        if (received &&
            (command.type == CommandType::kStop || command.type == CommandType::kCancelDrive)) {
            lease.Cancel();
            if (command.type == CommandType::kStop)
                SendStop();
            continue;
        }
        if (received && command.type == CommandType::kAction) {
            lease.Cancel();
            SendStop();
            SendAction("introduce");
            continue;
        }
        if (received && command.type == CommandType::kServo) {
            SendServo(command.first, command.second);
        }
        if (received && command.type == CommandType::kDrive) {
            driving = command;
            lease.Start(esp_timer_get_time(), command.duration_ms);
        }
        if (!lease.Active())
            continue;
        const int64_t now = esp_timer_get_time();
        if (lease.Expired(now)) {
            lease.Cancel();
            SendStop();
        } else if (lease.RefreshDue(now)) {
            // Short leases preserve the motion-board watchdog even for a
            // 60-second request. Losing this task/UART cannot latch the motors on.
            SendDrive(driving.first, driving.second, lease.Renew(now));
        }
    }
}
