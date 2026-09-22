# 传感器采集与 OLED 显示

按仓库最新硬件归档 `ProPrj_quadcopter_2026-09-04.epro2` 的 PCB 网络实现：
STM32F411 采集 MPU6050、BMP388、GPS 和 ADC，经 USART2 发给 ESP32-S3，由 ESP32 驱动 OLED。
需要分别烧录两个主控。

完整执行流程与通信细节见 [程序流程图](docs/PROGRAM_FLOW.md)。

## 接线与默认配置

| 功能 | 主控接口 | 对端 |
| --- | --- | --- |
| MPU6050 | STM32 I2C1：PB6 SCL / PB7 SDA | U2，自动识别 0x68 / 0x69，校验 WHO_AM_I |
| BMP388 | STM32 I2C2：PB10 SCL / PB3 SDA | U4，自动识别 0x76 / 0x77，校验 CHIP_ID=0x50 |
| GPS | STM32 USART1：PA9 TX / PA10 RX | U7 RX / TX，默认 9600、8N1 |
| OLED 数据发送 | STM32 USART2 PA2 TX | ESP32 GPIO18 RX，115200、8N1 |
| OLED 数据链路回线 | STM32 USART2 PA3 RX | ESP32 GPIO17 TX，当前无需发送回包 |
| OLED | ESP32 GPIO5 SCL / GPIO6 SDA | U6，默认 SSD1306 128×64，自动识别 0x3C / 0x3D |
| 电压采样 | STM32 PA1 / ADC1 channel 1 | 显示 ADC 原码及引脚电压 |

注意 CubeMX 的 PA2/PA3 标签和硬件网络名方向不一致，以表中的实际 TX/RX 引脚为准。GPS 经纬度保留 NMEA 度分格式，不是十进制度。

实物已确认沿用该 PCB，OLED 为 SSD1306 128×64。I²C 模块须有合适的上拉，信号电平为 3.3V；不能仅凭模块供电引脚接 5V 就认定其信号电平兼容。BMP388 模块须设置为 I²C 模式（CSB 高、SDO 地址确定），具体以模块电路为准。

STM32 参数在 `Core/Inc/sensor_config.h`。ADC 默认参考电压 3300mV，没有已知分压比，显示的是 PA1 电压，不是电池端电压。原工程 HSE 为 25MHz，实物晶振不同时须先调整时钟配置。

## 功能

- MPU6050：±2g、±250°/s、50Hz；显示三轴加速度（G）、三轴角速度（D/S）和芯片温度。
- BMP388：读取出厂校准系数，进行温度及压力补偿；25Hz，压力 8 倍过采样；显示 hPa 和摄氏度。
- GPS：中断接收、1024 字节环形缓冲，在主循环解析具有正确校验和的 GGA；支持 GP/GN 等 talker；显示定位质量、卫星数和经纬度。不配置 GPS 模块本身，须启用 NMEA GGA 输出并匹配波特率。
- OLED：每 200ms 更新当前页数据，不再自动切页；上电默认 MPU 页。KEY1（PA4）下一页、KEY2（PA5）上一页、KEY3（PC13）返回 MPU 首页，页面首行显示页码。按键采用 30ms 消抖，长按不连翻，松开后再次按下才触发。复位时已按住的键需先松开再按。
- 传感器通信失败立即清除有效标记，500ms 没有新采样也判为离线；每 2 秒尝试重新初始化。GPS 3 秒没有有效 GGA 显示超时，无定位 GGA 显示等待定位。
- ESP32 上电显示等待 STM32；有效显示帧中断超过 2 秒后清除旧数据并提示链路断开；OLED 掉线后每 2 秒重新探测。

这是传感器联调程序，主循环有短时间的阻塞 I²C、串口发送和初始化延时，尚不是飞控实时控制调度。代码没有启动电机 PWM，也没有姿态融合或气压高度估算。

## 构建和烧录

STM32 推荐直接使用 ARM GCC，无需 IAR。工具链必须是 `arm-none-eabi-gcc`，不是电脑本机的 `gcc`。本机已安装该工具链；其他 Debian/Ubuntu 机器可安装 `gcc-arm-none-eabi`、`binutils-arm-none-eabi`、`libnewlib-arm-none-eabi` 和 `make`。

在仓库根目录执行：

```sh
make -C software -j4
```

输出在 `software/build/`：
- `quad.elf`：带调试符号，可供 GDB/OpenOCD 使用。
- `quad.hex`：带地址信息，可供 STM32CubeProgrammer 使用。
- `quad.bin`：裸二进制，烧录起始地址为 **0x08000000**。
- `quad.map`：链接布局。

使用 ST-Link 和 OpenOCD 烧录（连接 SWDIO/PA13、SWCLK/PA14、GND，并保证目标板供电）：

```sh
make -C software flash
```

该命令会写入 Flash、校验并复位。默认编译命令不会连接或烧录设备。也可以在 STM32CubeProgrammer 中加载 HEX 烧录。固件按独立应用链接，不为自定义 bootloader 预留偏移。

`GCC/` 提供启动代码、512KiB Flash / 128KiB RAM 链接脚本及受限堆分配；保留 4KiB 栈、2KiB 堆，启用 Cortex-M4F 硬件单精度浮点。使用 newlib-nano，不依赖半主机调试。工具链中的 nosys 文件操作桩可能输出“未实现”的链接警告，当前应用使用内存格式化、不使用文件 I/O；不影响固件生成。

若使用 IAR：在 IAR 中打开 `EWARM/Project.eww`，编译 `software` 工程并下载。已将新增源文件和 I²C/UART HAL 源文件加入工程；不需要开启浮点 printf。

ESP32：使用 ESP-IDF 5.3 或更新版本（新 I²C master API、esp_driver_i2c/esp_driver_uart 组件），在已加载 IDF 环境的终端执行：

```sh
cd software/esp32_oled
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

串口设备路径按实物修改。`menuconfig → Quad sensor display` 可修改 OLED 引脚和 STM32 链路串口参数。该目录是独立固件；现有 `test/wifi_test` 的 Wi-Fi/OTA 实验程序没有合并到显示固件。

CubeMX 当前只把 I²C/UART 引脚设为复用，没有生成这些外设的初始化，本实现统一由 `SensorApp_Init` 管理。不要再添加第二套重复初始化。重新生成 CubeMX 代码后，须检查：
1. `stm32f4xx_hal_conf.h` 中 I²C/UART 模块依然启用。
2. IAR 工程中保留 `sensor_app.c`、`gps_nmea.c`、`bmp388_math.c` 和 I²C/UART HAL 源文件。
3. `main.c` 和中断文件的 USER CODE 中保留应用调用，ADC 采样时间为 144 cycles。

## 验证

仓库根目录执行：

```sh
python3 test/sensors/check.py
python3 test/sensors/check_firmware.py
```

脚本使用主机 C 编译器运行 CRC/截断/损坏帧恢复、GPS 校验及坐标范围、BMP388 补偿与系数符号测试；随后用 `arm-none-eabi-gcc` 编译 IAR 工程列出的全部 C 文件并检查链接符号。链接使用 nosys 桩，可能输出标准系统调用未实现的警告；产物仅做符号检查，不是可烧录固件。

已通过协议测试、24 个 ARM C 文件检查，以及 GCC 完整固件构建；额外检查所有中断向量、初始栈地址、复位入口、GPS/SysTick 中断绑定、内存布局和未解析符号。GCC 构建生成的 ELF/HEX/BIN 可用于烧录。当前环境没有 ESP-IDF / IAR，尚未执行 ESP32 完整构建、IAR 完整构建或实物测试。

上板验证：
1. 上电后 OLED 显示 MPU 首页，按 KEY1/KEY2 切换三页、KEY3 返回首页；若一直等待 STM32，核对 PA2→GPIO18 和共地。
2. 平放 MPU 时，加速度合量应接近 1G，静止角速度应接近零（存在零偏）；翻转或转动后数值应变化。
3. BMP 应显示所在地合理气压和芯片温度；GPS 在室外获得定位后显示有效坐标。
4. 断开单个传感器，确认对应页显示离线，按键切换后其他页面数据仍更新；重新接入后确认恢复。
5. 停止 STM32，确认 OLED 提示链路断开；复位后确认恢复。
6. 用万用表对照 PA1 电压，再根据实测 VREF 和真实分压比扩展电池电压换算。

## 协议与依据

`Common/display_link.h` 是两端共用协议：2 字节 QD 帧头、1 字节版本 1、8×21 字节 ASCII 行数据、2 字节小端 CRC16-CCITT-FALSE，共 173 字节。CRC 覆盖帧头、版本和文本，接收端校验后整帧替换；丢字节或损坏后滑动寻找下一帧。当前发送显示文本，如后续需要 ESP32 数值计算，可新增协议版本传输结构化测量值。

寄存器和协议核对资料：
- [TDK MPU6050 register map](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf)
- [Bosch BMP388 datasheet，校准与补偿见附录](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp388-ds001.pdf)
- [ESP-IDF I²C master API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2c.html)
- [ESP-IDF UART API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/uart.html)
- [u-blox NMEA GGA 字段说明](https://content.u-blox.com/sites/default/files/documents/u-blox-F9-HPG-1.32_InterfaceDescription_UBX-22008968.pdf)
