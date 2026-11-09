#include "camera_stream_server.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <esp_camera.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <img_converters.h>

#include "application.h"

namespace {
constexpr char kTag[] = "CameraWeb";
constexpr char kStreamContentType[] = "multipart/x-mixed-replace;boundary=walleframe";
constexpr char kBoundary[] = "\r\n--walleframe\r\n";
constexpr char kFrameHeader[] = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

constexpr char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>瓦力摄像头</title>
  <style>
    body{margin:0;background:#111;color:#eee;font-family:system-ui;text-align:center}
    main{max-width:760px;margin:auto;padding:20px}
    img{width:100%;height:auto;background:#000;border-radius:12px}
    a{color:#69b7ff} small{color:#aaa}
  </style>
</head>
<body><main>
  <h2>瓦力摄像头</h2>
  <img src="/stream" alt="camera stream">
  <p><a href="/camera.jpg" target="_blank">打开单张照片</a></p>
  <small>QVGA · 局域网 MJPEG · 同时允许一个观看设备</small>
</main></body></html>)HTML";

bool FrameToJpeg(camera_fb_t* frame, uint8_t** jpeg, size_t* jpeg_length) {
    if (frame->format == PIXFORMAT_JPEG) {
        *jpeg = frame->buf;
        *jpeg_length = frame->len;
        return true;
    }
    return frame2jpg(frame, 70, jpeg, jpeg_length);
}
}  // namespace

esp_err_t CameraStreamServer::IndexHandler(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t CameraStreamServer::SnapshotHandler(httpd_req_t* request) {
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "camera unavailable");
        return ESP_FAIL;
    }

    uint8_t* jpeg = nullptr;
    size_t jpeg_length = 0;
    const bool converted = frame->format != PIXFORMAT_JPEG;
    const bool ok = FrameToJpeg(frame, &jpeg, &jpeg_length);
    if (!ok || jpeg == nullptr || jpeg_length == 0) {
        esp_camera_fb_return(frame);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "jpeg encode failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(request, "image/jpeg");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store, no-cache, must-revalidate");
    const esp_err_t result = httpd_resp_send(request, reinterpret_cast<const char*>(jpeg),
                                              jpeg_length);
    if (converted) free(jpeg);
    esp_camera_fb_return(frame);
    return result;
}

esp_err_t CameraStreamServer::StreamHandler(httpd_req_t* request) {
    esp_err_t result = httpd_resp_set_type(request, kStreamContentType);
    if (result != ESP_OK) return result;
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    ESP_LOGI(kTag, "MJPEG viewer connected");
    while (result == ESP_OK) {
        camera_fb_t* frame = esp_camera_fb_get();
        if (frame == nullptr) {
            ESP_LOGW(kTag, "Camera frame unavailable");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint8_t* jpeg = nullptr;
        size_t jpeg_length = 0;
        const bool converted = frame->format != PIXFORMAT_JPEG;
        if (!FrameToJpeg(frame, &jpeg, &jpeg_length) || jpeg == nullptr || jpeg_length == 0) {
            esp_camera_fb_return(frame);
            result = ESP_FAIL;
            break;
        }

        char header[80];
        const int header_length = snprintf(header, sizeof(header), kFrameHeader,
                                           static_cast<unsigned>(jpeg_length));
        result = httpd_resp_send_chunk(request, kBoundary, strlen(kBoundary));
        if (result == ESP_OK)
            result = httpd_resp_send_chunk(request, header, header_length);
        if (result == ESP_OK)
            result = httpd_resp_send_chunk(request, reinterpret_cast<const char*>(jpeg),
                                           jpeg_length);

        if (converted) free(jpeg);
        esp_camera_fb_return(frame);

        // The audio websocket has priority over the optional camera preview.
        // A low preview rate is enough for remote observation and prevents a
        // slow browser from filling the ESP32 Wi-Fi transmit buffers.  Reduce
        // it further while listening/speaking, when audio is latency-sensitive.
        const DeviceState state = Application::GetInstance().GetDeviceState();
        const bool audio_active = state == kDeviceStateListening ||
                                  state == kDeviceStateSpeaking ||
                                  state == kDeviceStateConnecting;
        vTaskDelay(pdMS_TO_TICKS(audio_active ? 1000 : 400));
    }
    ESP_LOGI(kTag, "MJPEG viewer disconnected");
    return result;
}

void CameraStreamServer::PrintAddressTask(void* context) {
    auto* self = static_cast<CameraStreamServer*>(context);
    for (int attempt = 0; attempt < 120; ++attempt) {
        esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t info = {};
        if (station != nullptr && esp_netif_get_ip_info(station, &info) == ESP_OK &&
            info.ip.addr != 0) {
            ESP_LOGI(kTag, "Open camera at http://" IPSTR ":%u/", IP2STR(&info.ip),
                     self->port_);
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGW(kTag, "Camera server started, but Wi-Fi has no IP address yet");
    vTaskDelete(nullptr);
}

bool CameraStreamServer::Start(uint16_t port) {
    if (server_ != nullptr) return true;
    port_ = port;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.ctrl_port = 32770;
    config.max_uri_handlers = 4;
    config.max_open_sockets = 3;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    // Drop a stalled MJPEG client quickly instead of letting it monopolize
    // network buffers needed by the cloud audio websocket.
    config.send_wait_timeout = 1;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start camera HTTP server on port %u", port_);
        server_ = nullptr;
        return false;
    }

    const httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = IndexHandler};
    const httpd_uri_t snapshot = {
        .uri = "/camera.jpg", .method = HTTP_GET, .handler = SnapshotHandler};
    const httpd_uri_t stream = {
        .uri = "/stream", .method = HTTP_GET, .handler = StreamHandler};
    httpd_register_uri_handler(server_, &index);
    httpd_register_uri_handler(server_, &snapshot);
    httpd_register_uri_handler(server_, &stream);

    ESP_LOGI(kTag, "Camera HTTP server started on port %u", port_);
    xTaskCreate(PrintAddressTask, "camera_url", 3072, this, 2, nullptr);
    return true;
}
