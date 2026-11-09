#pragma once

#include <esp_http_server.h>

class CameraStreamServer {
public:
    bool Start(uint16_t port = 81);

private:
    static esp_err_t IndexHandler(httpd_req_t* request);
    static esp_err_t SnapshotHandler(httpd_req_t* request);
    static esp_err_t StreamHandler(httpd_req_t* request);
    static void PrintAddressTask(void* context);

    httpd_handle_t server_ = nullptr;
    uint16_t port_ = 81;
};
