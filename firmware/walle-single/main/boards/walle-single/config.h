#pragma once
#include <driver/gpio.h>
#define AUDIO_INPUT_SAMPLE_RATE 32000
#define AUDIO_OUTPUT_SAMPLE_RATE 32000
#define AUDIO_BCLK GPIO_NUM_13
#define AUDIO_WS GPIO_NUM_17
#define AUDIO_DOUT GPIO_NUM_18
#define AUDIO_DIN GPIO_NUM_12
#define MOTION_UART_PORT UART_NUM_2
#define MOTION_MQTT_URI "wss://walle.kangqing.net:443/mqtt"
#if __has_include("secrets.motion.h")
#include "secrets.motion.h"
#else
#define MOTION_MQTT_USERNAME ""
#define MOTION_MQTT_PASSWORD ""
#endif

// OLED has a dedicated I2C1 bus; PCA9685 remains on I2C0 GPIO10/11.
#define WALLE_OLED_SDA GPIO_NUM_47
#define WALLE_OLED_SCL GPIO_NUM_48
#define WALLE_OLED_WIDTH 128
#define WALLE_OLED_HEIGHT 64
