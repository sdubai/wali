# 实战派 WALL-E 控制扩展

该版本保留实战派原有双 MIC、AEC、喇叭、触摸屏和摄像头，通过 UART 控制另一块运动 ESP32，不再直接连接 PCA9685 或 L298N。

## UART

- GPIO10：TX，连接运动板 GPIO17（RX）
- GPIO11：RX，连接运动板 GPIO18（TX）
- GND：连接运动板 GND
- 波特率：115200，8N1

## 编译

在本固件目录执行：

```bash
python3 scripts/build.py lckfb/szpi-esp32s3 --name lichuang-dev
```

设备启动后应看到：

```text
I (...) MotionLink: UART ready: TX=10 RX=11 baud=115200
I (...) MCP: Add tool: self.robot.drive
I (...) MCP: Add tool: self.robot.stop
I (...) MCP: Add tool: self.servos.set_angle
```

测试时先架空履带并拆下舵机机械负载，确认方向、角度和急停后再落地运行。

