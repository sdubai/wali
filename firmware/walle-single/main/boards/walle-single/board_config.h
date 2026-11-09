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

// Independent 3.3V UART to Raspberry Pi GPIO15/RX and GPIO14/TX.
#define WALLE_PI_UART_PORT  UART_NUM_2
#define WALLE_PI_UART_TX    GPIO_NUM_8
#define WALLE_PI_UART_RX    GPIO_NUM_9

// L298N. Move or remove the ENA/ENB jumpers when PWM is connected.
#define WALLE_MOTOR_ENA     GPIO_NUM_4
#define WALLE_MOTOR_IN1     GPIO_NUM_5
#define WALLE_MOTOR_IN2     GPIO_NUM_6
#define WALLE_MOTOR_ENB     GPIO_NUM_7
#define WALLE_MOTOR_IN3     GPIO_NUM_15
#define WALLE_MOTOR_IN4     GPIO_NUM_16

// GPIO12 and GPIO13 are unused; HC-SR04 support has been removed.

// PS2 wireless receiver, 3.3V logic. Dual sticks and shoulder buttons.
#define WALLE_PS2_DAT       GPIO_NUM_1
#define WALLE_PS2_CMD       GPIO_NUM_2
#define WALLE_PS2_ATT       GPIO_NUM_14
#define WALLE_PS2_CLK       GPIO_NUM_21
#define WALLE_PS2_SPEED     100
#define WALLE_PS2_TIMEOUT_MS 200
// Degrees per second. Small accelerated steps keep joystick moves smooth.
#define WALLE_PS2_ARM_SPEED 40
#define WALLE_PS2_HEAD_SPEED 25
#define WALLE_PS2_EYE_SPEED 12

// Temporary reassembly calibration: all seven installed servos use 10..170
// independently. Set to 0 after installing the newly measured safe limits.
// PS2 servo gestures and introduction choreography are disabled in this mode.
#define WALLE_INDEPENDENT_SERVO_CALIBRATION 1

#define WALLE_SERVO_COUNT   12
#define WALLE_SERVO_MIN_DEG 10
#define WALLE_SERVO_MAX_DEG 170
// Eye lift servos have a much smaller safe mechanical travel after assembly.
// These are physical PCA9685 angles. Servo 3 (right eye) rises as the angle
// increases; servo 4 (left eye) is mirrored and rises as the angle decreases.
#define WALLE_SERVO_3_MIN_DEG 30
#define WALLE_SERVO_3_MAX_DEG 60
#define WALLE_SERVO_4_MIN_DEG 110
#define WALLE_SERVO_4_MAX_DEG 140
// Servo 5 rotates the neck: 10 right, 108 forward, 170 left.
#define WALLE_SERVO_5_MIN_DEG 10
#define WALLE_SERVO_5_CENTER_DEG 108
#define WALLE_SERVO_5_MAX_DEG 170
// Neck pitch is split across two interlocked servos.
// Level: servo 6 = 145, servo 7 = 70.
// Look down: keep servo 7 at 70 and move servo 6 from 145 toward 115.
// Look up: keep servo 6 at 145 and move servo 7 from 70 toward 150.
#define WALLE_SERVO_6_MIN_DEG 115
#define WALLE_SERVO_6_LEVEL_DEG 145
#define WALLE_SERVO_6_MAX_DEG 145
#define WALLE_SERVO_7_MIN_DEG 70
#define WALLE_SERVO_7_LEVEL_DEG 70
#define WALLE_SERVO_7_MAX_DEG 150
// The left arm servo is installed in the opposite direction. Keep commands in
// intuitive logical angles and mirror only the PCA9685 output for servo 1.
#define WALLE_SERVO_1_REVERSED 1
// Web/voice commands refresh while moving. This watchdog stops the chassis if
// the UART or network link disappears, while allowing enough jitter headroom.
#define WALLE_DRIVE_TIMEOUT_MS 1500

// Introduction choreography. Track traction and battery voltage affect the
// exact angle, so tune only TURN_60_DEG_MS after an on-floor test.
#define WALLE_INTRO_TURN_SPEED    35
#define WALLE_INTRO_TURN_60_DEG_MS 370

