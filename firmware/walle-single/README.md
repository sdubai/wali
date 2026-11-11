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
- 单颗ESP32负责联网对话、音频和运动，取消语音唤醒和摄像头，支持可选OLED状态屏。音频共用32kHz I2S时钟，内部转换至对话所需采样率；不启用本地AEC，先以轮流说话方式测试。
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

## OLED独立总线

默认支持四针I2C SSD1306、128×64屏：VCC→3.3V，GND→GND，SDA→GPIO47，SCL→GPIO42。
OLED独占I2C1，PCA9685继续使用I2C0的GPIO10/11。固件依次探测0x3C、0x3D地址；没有检测到屏或面板初始化失败时保留串口显示，不阻止语音和运动启动。
正常启动日志为 `WalleOLED: SSD1306 128x64 ready ...`。屏幕沿用现有OLED界面显示网络状态、聆听/说话状态、表情和对话文字。长文本受128×64屏幕空间限制，不是完整聊天记录。
若屏幕实际为SH1106、128×32或SPI版，需要更改驱动/尺寸，不能仅凭I2C地址识别控制芯片。GPIO48专用于启动时关闭板载WS2812 RGB灯，不再承载OLED时钟。

### 从OLED 47/48接线迁移

断电，将OLED SCL从GPIO48移到GPIO42；SDA保持GPIO47，VCC/GND不变。然后烧录本次固件。GPIO48不再外接OLED；启动日志应出现 `WalleRGB: Board RGB cleared on GPIO48`。RGB使用与本板一致的GPIO48 WS2812接法，电源指示灯不受影响。

### K2按下没有反应

K2只在待机状态启动测试。先按K1结束语音对话，确认屏幕回到待机，再按K2。忙碌时屏幕会提示先结束对话。
串口 `WalleKeys: K2 GPIO39 pressed` 表示检测到了实体按键；`K2 click received` 表示短按事件已到达；`K2 ignored: state=...` 表示当前状态不允许启动；`Test gesture skipped: PCA9685 not ready` 表示音效仍可测试，但舵机驱动板未就绪。没有pressed日志时优先检查K2是否接GPIO39、公共端是否接GND。

### OLED刷新失败与看门狗

本板启用同步I2C专用的刷新收尾回调：OLED传输失败、未触发完成通知时丢弃这一帧，防止LVGL在等待完成标志时空转。正常帧仍由原驱动完成回调通知；该恢复不适用于异步SPI/DMA屏。初始化时持有LVGL锁直到回调安装完毕，刷新限制为10Hz，OLED界面更新锁获取失败时不再继续操作对象。看门狗没有关闭或放宽。
若出现 `OLED transfer missed completion; dropped frame`，仍需检查OLED供电、47/42接线及接触，软件恢复不代表硬件故障已消失。

## X3S 蓝牙手柄（单板版）

单板固件新增 ESP-IDF 原生 NimBLE HID 主机，不需要额外蓝牙接收器或改 PCB。
绑定本机 X3S 的公共 MAC `03:26:07:03:CA:12`；可在 menuconfig 的
`Walle BLE gamepad` 中修改 `WALLE_BLE_GAMEPAD_MAC`。默认配置同时保存在
`config/walle-single.defaults` 与板级 `config.json` 中，后续使用构建脚本会重新应用这些默认值。
不扫描绑定其他手柄，不使用厂家例程中的演示 MAC。原双板语音程序不受影响。

1. 用 `./build-single.sh` 更新现有 sdkconfig 并编译，然后按原流程烧录；用户自行烧录。
2. X3S 关机后，按住 **X** 再按 **HOME**，进入 Android 标准 BLE HID 模式。
3. 日志出现 `WalleBLE: X3S HID ready` 后，松开所有按键、将两只摇杆回中，才会启用控制。
4. 左摇杆或方向键沿用 PS2 的履带控制，松开停车；BLE 有有效输入时优先于 PS2。
   BLE 失效后回到 PS2 时也须先回中，不能继承原来的操控状态。
5. 连接中断、输入格式错误、读报告失败或超过 350ms 没有有效报告，会释放手柄控制；
   现有运动轮询停止本地手柄驱动，电机自身仍保留 200ms 指令到期停车保护。
   HID 通知可能仅在输入变化时发送，因此后台每 100ms 读取当前输入，支持持续推杆，
   不会仅因为手柄没有重复发送同一通知而直接沿用无限期旧数据。

按 HID Report Map 解析 X/Y 和 Z/Rz 或 Rx/Ry，支持 8/16 位、有符号轴及 Report ID。
只接受包含完整四轴的可读、可通知输入报告，其他键盘/媒体报告不参与驾驶。
不支持的布局记录错误并停止蓝牙输入，不按猜测的固定字节偏移驱动电机。
厂家 Arduino 示例仅提供连接及原始数据打印；这里独立实现描述符解析和控制层。
没有引入 Arduino，也没有将厂家未声明许可的源码复制进工程。

按键采用 Android 常见 A/B/X/Y、LB/RB、LT/RT 顺序映射到现有 PS2 控制位，
实际 X3S 的按键对应和轴方向仍需架空履带、空载实机核对。日志每 3 秒打印
`BLE input armed=... buttons=... LX=... LY=... RX=... RY=...`，用于核对输入。

**现有 `WALLE_INDEPENDENT_SERVO_CALIBRATION=1` 保留不变：手柄的眼睛、手臂、头颈
动作仍不输出，网页逐路校准仍可用。** 测定机械范围后才取消该校准锁。
取消校准锁后的映射沿用 PS2：LB/LT 左臂、RB/RT 右臂、X/A 左眼、Y/B 右眼，
右摇杆控制摇头与分级俯仰；此改动不扩大机械角度范围。

蓝牙控制为新增硬件功能；编译及主机模拟测试不代表实机验证，首次测试先架空履带，
保持舵机和电机供电可随时断开。已有音频解码器初始化失败不由此功能修复。
