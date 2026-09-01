# esp32-example
乐鑫esp32 的一些功能测试工程

## light_sleep
用于测试 ESP32 浅睡眠（light sleep）功耗的示例工程。进入浅睡眠后可通过 BOOT 按键唤醒。

## uvc
USB 摄像头（UVC Host）示例工程：连接 USB 摄像头后采集 MJPEG 视频流，并通过 Wi-Fi SoftAP 在浏览器中实时预览。

预览方式：
1. 烧录运行后，手机/电脑连接热点：SSID `ESP-UVC`，密码 `12345678`
2. 浏览器打开 http://192.168.4.1 即可看到摄像头画面

支持芯片：ESP32-S2 / ESP32-S3 / ESP32-P4（需 USB-OTG，建议外接 PSRAM）。
