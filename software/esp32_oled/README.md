# ESP32-S3 手机遥控与 OLED

当前固件不使用 NRF24。ESP32-S3 自建 Wi-Fi 热点并提供手机网页，收到网页控制量后通过 UART1（GPIO17 TX）向 STM32F411 USART2（PA3 RX）发送 QCTRL v1 控制帧；F411 USART2 PA2 TX→ESP32 GPIO18 RX 仍用于传感器/OLED 数据。两端须共地。OLED SSD1306 128×64 使用 GPIO5 SCL / GPIO6 SDA。

默认热点名称 `QUAD-CONTROL`，密码 `quad-phone-2026`，网页地址 `http://192.168.4.1/`。在 `idf.py menuconfig → Quad phone controller` 修改热点名称和密码后重新构建、烧录；一个热点只允许一台手机连接。请在手机 Wi-Fi 设置中手动连接热点，再用浏览器输入 **http** 地址。

网页有两根虚拟摇杆：左杆左右控制 YAW、上下控制 THR；右杆左右控制 ROLL、上下控制 PITCH。点击“开始控制”后约每 50ms 发送一次原码 0～4095；松开左杆油门归零，松开右杆回中。停止控制、切到后台或关闭页面后，网页停止持续发送。ESP32 没有新指令时不再发 UART 控制帧，F411 的 `ControlApp_GetLatest()` 在 250ms 后返回无效。OLED 默认显示手机控制页，网页按钮可切换为 STM32 传感器页；传感器页仍由 F411 的 SW5 拨码选择。OLED 的手机页超时后不显示旧摇杆值。

F411 当前只接收和保存 QCTRL 指令，尚无电机 PWM、姿态控制或飞行级保护。手机网页和 Wi-Fi 链路先用于拆桨台架联调。

## 编译与烧录

```sh
source ~/.espressif/v6.1/esp-idf/export.sh
cd ~/quad/software/esp32_oled
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

串口号按实际设备修改。当前电脑的 ESP-IDF v6.1 Python 虚拟环境未安装；若 `export.sh` 报错，先执行 `~/.espressif/v6.1/esp-idf/install.sh esp32s3`。首次配置时建议在 `menuconfig` 修改默认热点密码。
