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
## dvp_s31_wifi

ESP32-S31 + GC2145 DVP 摄像头 Wi-Fi 预览示例，仅包含摄像头采集、JPEG 编码和浏览器预览功能。

摄像头输出 640×480 RGB565 大端彩色数据，由 LCD_CAM 提供 20MHz MCLK，再通过软件编码为 JPEG，经 Wi-Fi 传输到手机或电脑。网页显示摄像头实际采集的画面，不使用测试图；实测约 7 帧/秒，会有压缩和传输延迟。

预览方式：

1. 烧录运行后，手机/电脑连接热点：SSID `ESP-S31-CAM`，密码 `12345678`。提示没有互联网时保持连接。
2. 浏览器打开 [http://192.168.4.1](http://192.168.4.1) 查看实时画面。
3. 点击页面下载链接，或访问 [http://192.168.4.1/capture](http://192.168.4.1/capture)，获取最新 JPEG 图片。

支持芯片及硬件：ESP32-S31、GC2145 DVP 摄像头、16MB Flash 和 16MB PSRAM。视频流使用 81 端口，目前支持一个实时观看端。

使用支持 ESP32-S31 的 ESP-IDF 6.1 编译：

```powershell
cd dvp_s31_wifi
idf.py set-target esp32s31
idf.py build
idf.py -p COM1328 flash monitor
```

串口会输出摄像头识别结果、帧亮度范围、JPEG 大小及编码耗时，可用于排查暗画面、曝光和采集问题。实际帧率随场景和编码耗时变化。

详细引脚、电源配置及说明见 [dvp_s31_wifi/README.md](dvp_s31_wifi/README.md)。

S31 示例目录同时保留合并固件 `dvp_s31_wifi.bin`、合并工具 `combine.exe` 和校验文件，可直接烧录使用；具体命令见示例说明。
