#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class MotionLink {
public:
    MotionLink();
    ~MotionLink() = default;

    void RegisterTools();

private:
    enum class CommandType : unsigned char {
        kDrive,
        kServo,
        kStop,
    };

    struct Command {
        CommandType type;
        int first;
        int second;
        int duration_ms;
    };

    QueueHandle_t queue_ = nullptr;

    bool Submit(const Command& command, bool urgent = false);
    void Run();
    void SendDrive(int left, int right);
    void SendServo(int servo, int angle);
    void SendStop();
    void WriteLine(const char* line);
};
