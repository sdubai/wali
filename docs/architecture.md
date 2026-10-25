# 系统架构

## 实战派交互板

交互板保留官方实战派的音频驱动、双麦克风采集、回声消除、扬声器、LCD、触摸和摄像头。小智将自然语言转换为动作后，通过 UART 发送 JSON 指令，不直接驱动大电流负载。

实战派注册以下设备端 MCP 工具：

- `self.robot.drive(left, right, duration_ms)`：前进、后退和转向。
- `self.robot.stop()`：立即向运动板发送停止。
- `self.servos.set_angle(servo, angle)`：控制 1～7 号舵机。

实战派会每 250ms 刷新一次正在执行的行走指令，并在时间结束、被新命令打断或收到停止要求时发送 `stop`。运动板同时保留独立的 700ms 行走看门狗。

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
{"cmd":"stop"}
```

`left`、`right` 范围为 `-100..100`。负数代表反转。舵机编号为 `1..7`，角度默认限制为 `10..170`。

MQTT 主题：

- `walle/motion/drive/set`：载荷为上述 `drive` JSON。
- `walle/motion/servo/set`：载荷为上述 `servo` JSON。
- `walle/motion/stop`：任意非空载荷触发急停。
- `walle/motion/status`：运动板在线状态和执行结果。

挥手、握手、抬头等组合动作将在运动板动作编排器中增加；当前实战派已经可以通过多个舵机角度工具调用组成动作。

