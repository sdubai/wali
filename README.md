# WALL-E（瓦力）机器人

本仓库保存瓦力机器人的两块 ESP32 固件、局域网控制页面、服务端源码、接线文档和 3D 打印修改件。

## 系统组成

```text
嘉立创实战派 ESP32-S3（语音、双 MIC、喇叭、屏幕、摄像头）
                    │ UART 3.3V
                    ▼
运动控制 ESP32（动作仲裁、MQTT、失联保护）
          ├── PCA9685 ── 7 个舵机
          └── L298N   ── 左右履带电机

网页 / Home Assistant ── MQTT/EMQX ── 运动控制 ESP32
```

## 目录

- `firmware/voice-terminal`：基于 xiaozhi-esp32 v2.5.2 的实战派固件，负责语音、屏幕和摄像头。
- `firmware/motion-controller`：精简运动控制固件，负责 PCA9685、L298N、UART 与 MQTT。
- `server`：本地 xiaozhi-server 源码及部署文件，不包含真实密钥。
- `web-controller`：手机可访问的局域网遥控页面。
- `docs`：架构、接线、协议和部署说明。
- `hardware/models`：已经修改的 STL 文件。

## 当前硬件映射

| 设备 | 接口 |
| --- | --- |
| 实战派 UART TX/RX | GPIO10 / GPIO11 |
| 运动板 UART RX/TX | GPIO17 / GPIO18 |
| 运动板 PCA9685 SDA/SCL | GPIO10 / GPIO11 |
| PCA9685 地址 | `0x40` |
| EMQX | `192.168.31.248:1883`，网页 WebSocket `8083` |

UART 必须交叉连接并共地。PCA9685 的舵机电源和 L298N 的电机电源应独立供电，所有电源只连接公共 GND。

详细步骤见 [docs/architecture.md](docs/architecture.md) 和 [docs/wiring.md](docs/wiring.md)。

## 安全配置

真实 Wi-Fi 密码、MQTT 密码、云服务 AK/SK、设备令牌和 `server/config.yaml` 都不会提交。请从 `.example` 文件复制本地配置，并仅在部署机器保存真实值。

## 上游项目

- [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
- [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)

