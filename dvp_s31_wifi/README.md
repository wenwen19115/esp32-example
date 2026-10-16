# ESP32-S31 GC2145 Wi-Fi 预览

独立摄像头调试固件，仅包含 DVP 采集、JPEG 编码和 Wi-Fi 浏览器预览。
支持 GC2145 640×480 彩色画面，硬件目标为 ESP32-S31。

## 复用来源

- Wi-Fi 热点及 MJPEG HTTP 服务：同仓库 `../uvc/main/web_preview.c`。
- 传感器引脚、TCA9555 电源与初始化复位：用户提供的 S31 DVP demo。
- GC2145 VGA 寄存器表：乐鑫 esp-video-components，来源提交及许可证见 `main/gc2145_vga_settings.h`、`main/GC2145-LICENSE`。
- 采集：IDF 6.1 `esp_driver_cam`，LCD_CAM 输出 20MHz MCLK。
- 编码：`espressif/esp_new_jpeg`，直接处理 RGB565 大端数据。

原 UVC 工程的 USB 摄像头通常直接输出 MJPEG；GC2145 输出 RGB565，因此采集部分不能照搬。
这里使用 DVP 连续采集、PSRAM 缓冲、软件 JPEG 编码，复用浏览器预览服务。
首页和截图在 80 端口，视频流在 81 端口，避免视频流阻塞截图请求。

## 手机预览

1. 烧录并启动设备。
2. 手机连接热点 **ESP-S31-CAM**，密码 **12345678**；提示没有互联网时保持连接。
3. 浏览器打开 **http://192.168.4.1/**。
4. 首页显示视频；点击下载链接或访问 `/capture` 获取最新 JPEG。
5. 暂只支持一个实时视频观看端；断开后可以重新打开。

串口每秒输出帧号、灰度范围、平均亮度、JPEG 大小与编码耗时。
手机画面全白时，结合串口灰度范围与平均亮度继续检查原始图像及曝光。
软件编码的实际帧率以设备表现为准，不保证传感器标称帧率。

## 硬件

- ESP32-S31，16MB Flash、16MB PSRAM，GC2145。
- I2C1 SDA=GPIO0、SCL=GPIO1；TCA9555=0x20、GC2145=0x3C。
- D0..D7=GPIO46..53，PCLK=54、MCLK=55、VSYNC=56、HREF=57。
- TCA P0.5：摄像头电源；P1.2：复位。

启动时按参考 demo 打开电源、执行复位，再输出 MCLK 并识别 GC2145。

## 编译及烧录

使用支持 S31 的 ESP-IDF 6.1，安装依赖后执行：

```powershell
idf.py set-target esp32s31
idf.py build
idf.py -p COM1328 flash monitor
```

依赖仅有软件 JPEG 组件。烧录使用本示例自带的分区表和完整烧录命令。

## 发布文件与合并固件烧录

目录内保留以下发布文件：

- `dvp_s31_wifi.bin`：合并固件，包含 bootloader、分区表和应用，烧录起始地址为 **0x0**。
- `combine.exe`：固件合并工具，与同仓库 UVC 示例使用的工具一致。
- `SHA256SUMS`：上述文件的 SHA-256 校验值。

合并固件已核对与当前编译产物一致。无需编译即可烧录：

```powershell
python -m esptool --chip esp32s31 --port COM1328 -b 460800 --no-stub write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 dvp_s31_wifi.bin
```

重新合并时按 `build/flash_args` 配置地址：bootloader 为 `0x2000`，分区表为 `0x8000`，应用为 `0x10000`，合并输出从 `0x0` 开始。
`build/dvp_s31_wifi.bin` 是单独应用，与目录中的同名合并固件不同。
构建目录、下载的组件、临时日志和本地 `sdkconfig` 不纳入上传；编译使用 `sdkconfig.defaults` 和 `dependencies.lock`。

## 官方参考

- https://github.com/espressif/esp-video-components/tree/master/esp_video/examples/simple_video_server
- https://github.com/espressif/esp-video-components/blob/master/esp_video/idf_component.yml
- ESP-IDF 6.1 本地 `examples/peripherals/usb/host/uvc` 的支持列表包含 ESP32-S31。
