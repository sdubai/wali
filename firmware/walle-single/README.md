# 瓦力单板语音运动版 v1

独立工程为 firmware/walle-single，板型 walle-single，发布名 walle-single-16m。原 voice-terminal、motion-controller 工程没有修改。不要选原嘉立创屏幕板板型；它会使用冲突的引脚。

本版目标为 ESP32-S3、16MB Flash、八线PSRAM（N16R8配置）。若你的运动板不是这个规格，请先调整构建配置再刷机。

## 功能和接线

- K1 / GPIO38：单击开始或结束语音对话；长按3秒配网。
- K2 / GPIO39：空闲时播放测试提示音并执行约9秒眼睛/手臂动作；再按停止。对话期间不启动表演。没有瓦力音乐素材，当前声音仅为测试提示音。
- K3 / GPIO40、K4 / GPIO41：预留。
- 四键公共端接GND，固件使用低电平按键。
- 音频SD→12，SCK和BCLK→13，WS和LRCK→17，DIN→18，GND共地，SPK接喇叭。供电沿用已确认模块规格，不接舵机电源。
- PCA9685、L298N、PS2和树莓派UART2（TX8/RX9）沿用原接线；17、18不得再接旧语音板。
- 单颗ESP32负责联网对话、音频和运动，取消语音唤醒、屏幕和摄像头。音频共用32kHz I2S时钟，内部转换至对话所需采样率；不启用本地AEC，先以轮流说话方式测试。
- 保留原独立舵机校准模式10–170度，6/7互不联动；PS2的舵机快捷动作仍按原校准模式禁用，履带手柄可用。K2只动1–4号，不驱动履带和颈部。新脖子完成实测前不恢复旧颈部自动动作。

## 编译

激活 ESP-IDF 6.0.3 环境后，在本目录执行 `./build-single.sh`，或手动执行：

```sh
cd firmware/walle-single
export XIAOZHI_BUILD_DIR=/tmp/walle-single-build
python scripts/build.py walle-single --name walle-single-16m --language zh-CN
```

构建目录特意使用英文临时路径，绕过当前工具链处理中文路径时的 specs 错误。源码保存在本目录。

## 烧录

编译成功后，在 firmware/walle-single 目录、已激活的ESP-IDF环境中执行（替换串口）：

```sh
idf.py -B /tmp/walle-single-build -p /dev/cu.usbmodemXXXX flash monitor
```

首次从纯运动固件切换，使用完整 flash 命令更新分区表、引导程序和应用，不要只烧 app。无须先 erase-flash，避免不必要地清除旧网络数据。新语音程序首次使用可能需要重新配网和在原后台绑定新设备。

OTA/对话入口沿用 https://walle.kangqing.net/xiaozhi/ota/ 。板型为新的 walle-single；不要向此板推送旧屏幕语音版或纯运动版的OTA应用。

## 首次验证

先关闭舵机和电机动力电源，保留ESP32与音频供电。长按K1配网，在后台激活/绑定设备后，验证K1录音、回复播放、再次按下结束；再测试K2提示音和再次按下停止。随后开启动力电源，检查眼睛/手臂动作、网页和手柄控制、松手停止。最后测试断网、重连和长按重新配网。这里没有执行实物测试或自动烧录。

## 更换音乐

替换 main/boards/walle-single/startup.ogg 后重新编译。文件必须是单声道 Ogg/Opus，不能仅把MP3改后缀；例如用ffmpeg转换：

```sh
ffmpeg -i music.mp3 -ac 1 -ar 16000 -c:a libopus -b:a 24k -frame_duration 60 startup.ogg
```

当前动作序列约9秒；较长音乐会继续播放，K2可中止，动作不会按音乐节拍自动重编排。

## 本地MQTT配置

GitHub不保存账号密码。网页运动控制可在设备联网后，通过其局域网IP的82端口配置MQTT连接；也可自行创建 `main/boards/walle-single/secrets.motion.h`（已被Git忽略）：

```cpp
#pragma once
#define MOTION_MQTT_USERNAME "your-username"
#define MOTION_MQTT_PASSWORD "your-password"
```

语音对话沿用原后台设备绑定流程。没有MQTT凭据时不影响K1语音对话和K2本地测试；网页运动控制需要完成MQTT配置。

## 验证

本版本在ESP-IDF 6.0.3完整编译通过，固件约2.1MiB；5项单板/运动租约测试、PS2主机回归和Ogg解析测试通过。未执行硬件烧录及实物测试。上游全板型测试套件依赖其他板型和CI资源，在本独立工程中不作为已通过的验证。

## VS Code板型配置

本独立工程默认选择 `WALLE_SINGLE`，不得使用 `BREAD_COMPACT_WIFI`。若曾经用默认板型生成过sdkconfig，先执行 `./build-single.sh` 迁移已有配置；随后可用ESP-IDF扩展直接构建。错误板型会在CMake配置阶段给出明确提示，不再等到链接时报 `create_board` 缺失。
