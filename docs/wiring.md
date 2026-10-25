# 接线说明

## 两块 ESP32 之间

| 实战派 | 运动板 | 说明 |
| --- | --- | --- |
| GPIO10 TX | GPIO17 RX | 实战派发送动作 |
| GPIO11 RX | GPIO18 TX | 运动板返回状态 |
| GND | GND | 必须共地 |

不要连接两块板子的 5V 输出。

## PCA9685

| PCA9685 | 运动板 |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO10 |
| SCL | GPIO11 |
| V+ | 独立 5～6V 舵机电源正极 |

舵机电源建议至少 10A，并在 PCA9685 的 V+ 和 GND 附近并联 `1000～2200µF` 电解电容。MG996R 不得由 ESP32 的 5V 引脚供电。

| PCA9685通道 | 功能 |
| ---: | --- |
| 0 | 左臂 |
| 1 | 右臂 |
| 2 | 左侧脖子抬升 |
| 3 | 右侧脖子抬升 |
| 4 | 脖子旋转 |
| 5 | 左眼升降 |
| 6 | 右眼升降 |

## L298N

默认固件引脚集中定义在 `firmware/motion-controller/main/board_config.h`，可以按实际接线修改。

| L298N | 运动板默认引脚 |
| --- | --- |
| ENA | GPIO4（PWM） |
| IN1 | GPIO5 |
| IN2 | GPIO6 |
| ENB | GPIO7（PWM） |
| IN3 | GPIO15 |
| IN4 | GPIO16 |

OUT1/OUT2 接左电机，OUT3/OUT4 接右电机。电机电源进入 L298N 的电机电源端，ESP32 与 L298N 必须共地。

