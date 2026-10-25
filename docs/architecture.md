# 系统架构

## 实战派交互板

交互板保留官方实战派的音频驱动、双麦克风采集、回声消除、扬声器、LCD、触摸和摄像头。小智将自然语言转换为动作后，通过 UART 发送 JSON 指令，不直接驱动大电流负载。

## 运动控制板

运动板只负责实时运动：

1. UART 接收小智动作。
2. MQTT 接收网页和 Home Assistant 指令。
3. PCA9685 控制七个舵机。
4. L298N 控制左右履带及 PWM 速度。
5. 行走命令超时后自动停车。

同一时刻只执行一个底盘速度目标。急停命令拥有最高优先级。预设动作可以控制舵机，但不能绕过舵机角度限制。

## 指令格式

每条 UART 指令是一行 JSON，以换行符结束：

```json
{"cmd":"drive","left":60,"right":60,"duration":1000}
{"cmd":"servo","id":1,"angle":120}
{"cmd":"action","name":"wave"}
{"cmd":"stop"}
```

`left`、`right` 范围为 `-100..100`。负数代表反转。舵机编号为 `1..7`，角度默认限制为 `10..170`。

MQTT 主题：

- `walle/motion/drive/set`：载荷为上述 `drive` JSON。
- `walle/motion/servo/set`：载荷为上述 `servo` JSON。
- `walle/motion/action/set`：载荷为动作名称。
- `walle/motion/stop`：任意非空载荷触发急停。
- `walle/motion/status`：运动板在线状态和执行结果。

