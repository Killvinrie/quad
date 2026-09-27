# ESP32-S3 手机遥控与 OLED

当前固件不使用 NRF24。ESP32-S3 自建 Wi-Fi 热点并提供手机网页，收到网页控制量后通过 UART1（GPIO17 TX）向 STM32F411 USART2（PA3 RX）发送 QCTRL v1 控制帧；F411 USART2 PA2 TX→ESP32 GPIO18 RX 仍用于传感器/OLED 数据。两端须共地。OLED SSD1306 128×64 使用 GPIO5 SCL / GPIO6 SDA。

默认热点名称 `QUAD-CONTROL`，密码 `quad-phone-2026`，网页地址 `http://192.168.4.1/`。在 `idf.py menuconfig → Quad phone controller` 修改热点名称和密码后重新构建、烧录；一个热点只允许一台手机连接。请在手机 Wi-Fi 设置中手动连接热点，再用浏览器输入 **http** 地址。

网页有两根虚拟摇杆：左杆左右控制 YAW、上下控制 THR；右杆左右控制 ROLL、上下控制 PITCH。点击“开始控制”后约每 50ms 发送一次原码 0～4095；左杆油门随垂直位置线性变化，横向偏航不改变油门，松手后油门保持而偏航回中；右杆松手回中。点停机、停止控制或切到后台会将油门归零并发送停机请求；左杆异常取消触摸也会停机。油门归零且其他轴回中时，可点“请求解锁”；该按钮只发送请求，不显示 F411 的实际状态。**拆桨、未解锁、油门为 0 时**，按住 `CH1`～`CH4` 可逐路识别物理 PWM 输出。ESP32 没有新指令时不再发 UART 控制帧，F411 在 250ms 后判链路失效。OLED 默认显示手机控制页，网页按钮可切换为 STM32 数据页；数据页仍由 F411 的 SW5 拨码选择。`0100`（仅 SWITCH3 接通）显示四路电机 PWM 和实际解锁/PID 状态。OLED 的手机页超时后不显示旧控制量。

F411 已启用 X 型姿态 PID 与混控，但初始增益未经飞行调参。请先看[电机拆桨联调及行程校准](../MOTORS.md)。手机网页和 Wi-Fi 链路先用于拆桨台架联调。

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
