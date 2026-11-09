# bai-dev

独立 ESP32-S3 板型：SSD1315 128×64 I2C OLED、ICS43434 麦克风、MAX98357A 功放和五路 PCA9685。没有 LCD、触摸屏或摄像头初始化。原实战派板型保留。

## 接线

| 模块引脚 | ESP32-S3 GPIO |
|---|---|
| OLED SDA | 41 |
| OLED SCK/SCL | 42 |
| MIC SCK 与功放 BCLK（并接） | 14 |
| MIC WS 与功放 LRCLK（并接） | 13 |
| MIC SD | 12 |
| 功放 DIN | 45 |
| PCA9685 SDA | 10 |
| PCA9685 SCL | 11 |

所有 GND 共地。OLED、音频一体板及 PCA9685 VCC 使用 3.3V；舵机电源接 PCA9685 V+，使用符合舵机规格的独立电源，不能从 ESP32 3V3 供电。喇叭接模块 SPK 两端。

这是新的独立开发板配置，不能直接用于仍连接板载音频/屏幕的实战派硬件。GPIO45 为启动配置相关引脚，外设不得在复位时强拉其电平。

OLED 使用 SSD1306 兼容驱动，自动探测 0x3C/0x3D；未检测到屏幕时继续运行音频与舵机。使用现有 OledDisplay 显示表情和状态。

音频使用 NoAudioCodecDuplex，同一 I2S 控制器输出共用时钟，输入输出均为 24kHz，麦克风读取左声道。不能用两个独立 Simplex 时钟输出驱动并接的线路。如录音无数据，检查模块麦克风 L/R 配置。没有硬件回声参考，不启用设备端 AEC。

## 编译

在工程根目录、已加载 ESP-IDF 的终端执行：

```sh
python3 scripts/build.py lckfb/bai-dev --name bai-dev
```

也可以在 menuconfig 的板型列表选择 `bai-dev (ESP32-S3 OLED + I2S + PCA9685)`。自动构建的产物名含厂家前缀 `lckfb-bai-dev`。

按用户要求，本次只修改代码，未编译、刷写或进行硬件测试。

## 舵机与 MQTT

五个舵机编号 1–5 对应 PCA9685 通道 0–4；当前测试角度范围 60–120 度，上电不主动运动。校准与范围见 servo_config.h。

保留 MCP 工具 self.servos.set_angle 和 self.servos.stop_all。MQTT broker 为 mqtt://192.168.31.248:1883；新板使用独立主题，避免控制旧板：

- 指令：walle/bai-dev/servo/0/set 至 walle/bai-dev/servo/4/set
- 内容：角度整数，例如 90；不要设置 retained
- 状态：walle/bai-dev/status
- Home Assistant 自动发现设备标识：bai_dev

已有旧主题的 HA 自动化需要改成上述新主题。BOOT 单击用于开始/结束对话，启动阶段点击进入 Wi-Fi 配网。
