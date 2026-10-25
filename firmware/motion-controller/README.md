# 运动控制固件

这是不包含屏幕、麦克风、喇叭和小智协议栈的独立 ESP-IDF 工程。

```bash
idf.py set-target esp32s3
idf.py menuconfig  # 填写 WALL-E motion controller 中的 Wi-Fi/MQTT
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

已实现 UART JSON、MQTT、PCA9685 七路舵机、L298N 双电机 PWM 和 700ms 自动停车。

