# 实战派 WALL-E 控制扩展

该版本保留实战派原有双 MIC、AEC、喇叭、触摸屏和摄像头，通过 UART 控制另一块运动 ESP32，不再直接连接 PCA9685 或 L298N。

## UART

支持通过现有 UART 将当前已连接 Wi-Fi 的 SSID/密码同步给树莓派。
树莓派发起请求，由运动板 UART2 中转；本板从 Wi-Fi 驱动读取当前个人/开放网络的配置，
只返回本地串口，不上传 MQTT，不打印密码。无需占用本板额外 GPIO。
部署与接线见仓库 `raspberry-pi/wifi-sync/README.md`，需要同时更新两块 ESP32。

- GPIO10：TX，连接运动板 GPIO17（RX）
- GPIO11：RX，连接运动板 GPIO18（TX）
- GND：连接运动板 GND
- 波特率：115200，8N1

## MQTT 连接配置（NVS）

首次刷入此版本后，未配置的设备使用编译时的 MQTT 参数。
在手机或电脑连接与语音板相同的 Wi-Fi 后，打开 `http://语音板IP:82/`。
输入设备当前 MQTT 密码验证，然后填写新的加密 MQTT 地址、用户名和密码，
点击“保存并重新连接”。参数以一条配置记录写入 NVS，断电或普通烧录后保留，
无需因为修改密码再次编译固件。擦除 Flash 会清除此配置。

公网地址为 `wss://walle.kangqing.net:443/mqtt`。支持 `wss://` 和 `mqtts://`。
此页面只修改设备端参数；EMQX 中的用户密码和运动控制网页也需要保持一致。
更换服务端密码后，在此处验证时仍应填写设备上次保存的旧密码。
配置页仅供可信局域网使用，不要通过公网反向代理暴露。

## 编译步骤

默认唤醒词为 **Hi，瓦力（嗨，瓦力）**，使用 `wn9_hiwalle_tts2`。
本板型不启用“你好瓦力”的自定义 MultiNet 命令词，避免模型与喊法不一致。

在本固件目录执行：

```bash
python3 scripts/build.py lckfb/szpi-esp32s3 --name lichuang-dev --language zh-CN --wake-word wn9_hiwalle_tts2
```

设备启动后应看到：

```text
I (...) MotionLink: UART ready: TX=10 RX=11 baud=115200
I (...) MCP: Add tool: self.robot.drive
I (...) MCP: Add tool: self.robot.stop
I (...) MCP: Add tool: self.servos.set_angle
```

测试时先架空履带并拆下舵机机械负载，确认方向、角度和急停后再落地运行。

## 语音行驶速度与时间

`self.robot.drive` 未填写速度时，左右履带默认100%；工具说明要求未明确指定慢速时使用100%，
后退/转向按对应方向设置正负。大模型仍负责理解方向与显式速度，串口日志
`Voice drive: left=...% right=...% duration=...ms` 会记录实际接收的参数。
时长默认10秒，可指定100～60000毫秒，例如“前进20秒”；“停止”调用 `self.robot.stop`。

语音板每250ms续发一次、每次最多750ms的UART行驶指令，到期自动停止；
运动板原来的1500ms最大看门狗保留，不能仅把看门狗改长来实现长时间行驶。
语音任务停转或UART中断后，运动板在最后一个短租约到期后停车。
网页原有500ms短指令不会被延长为10秒，新的方向/时长替换旧命令。
介绍动作和停止会取消当前续行，普通舵机命令可与行驶并行。

需烧录语音板和最新运动板；运动板在PS2接管履带时回传
`{"type":"motion_override"}`，语音板取消旧移动，松开手柄后不会恢复旧语音命令。
固件工具信息在设备重新连接后更新，不需修改服务端工具列表或公网部署。

# HC-SR04 距离回传

运动板通过 UART 返回单行 JSON，例如：
`{"type":"distance","valid":true,"distance_cm":35.6}`。
语音板独立接收任务校验类型、有效标记及 2～400cm 范围，再经现有 MQTT 连接
发布到 `walle/motion/distance`（非 retained）。无效回波为 `valid:false`、
`distance_cm:null`。断网时丢弃读数，不缓存在应用层供重连补发。

需要同时更新运动板和语音板固件。接线：运动板 TX18 → 本板 RX11；
本板 TX10 → 运动板 RX17；GND 共地。无需让运动板单独联网，网页无需更改。

