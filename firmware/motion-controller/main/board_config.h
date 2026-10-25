#pragma once

#include "driver/gpio.h"

// PCA9685
#define WALLE_I2C_PORT      I2C_NUM_0
#define WALLE_I2C_SDA       GPIO_NUM_10
#define WALLE_I2C_SCL       GPIO_NUM_11
#define WALLE_PCA9685_ADDR  0x40

// UART connected to the Lichuang voice board.
#define WALLE_UART_PORT     UART_NUM_1
#define WALLE_UART_RX       GPIO_NUM_17
#define WALLE_UART_TX       GPIO_NUM_18
#define WALLE_UART_BAUD     115200

// L298N. Move or remove the ENA/ENB jumpers when PWM is connected.
#define WALLE_MOTOR_ENA     GPIO_NUM_4
#define WALLE_MOTOR_IN1     GPIO_NUM_5
#define WALLE_MOTOR_IN2     GPIO_NUM_6
#define WALLE_MOTOR_ENB     GPIO_NUM_7
#define WALLE_MOTOR_IN3     GPIO_NUM_15
#define WALLE_MOTOR_IN4     GPIO_NUM_16

#define WALLE_SERVO_COUNT   7
#define WALLE_SERVO_MIN_DEG 10
#define WALLE_SERVO_MAX_DEG 170
#define WALLE_DRIVE_TIMEOUT_MS 700

