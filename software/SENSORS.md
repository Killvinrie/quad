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
| 手机控制指令 | STM32 USART2 PA3 RX | ESP32 GPIO17 TX，115200、8N1 |
| OLED | ESP32 GPIO5 SCL / GPIO6 SDA | U6，默认 SSD1306 128×64，自动识别 0x3C / 0x3D |
| 电压采样 | STM32 PA1 / ADC1 channel 1 | 显示 ADC 原码及引脚电压 |

注意 CubeMX 的 PA2/PA3 标签和硬件网络名方向不一致，以表中的实际 TX/RX 引脚为准。GPS 经纬度保留 NMEA 度分格式，不是十进制度。

实物已确认沿用该 PCB，OLED 为 SSD1306 128×64。I²C 模块须有合适的上拉，信号电平为 3.3V；不能仅凭模块供电引脚接 5V 就认定其信号电平兼容。BMP388 模块须设置为 I²C 模式（CSB 高、SDO 地址确定），具体以模块电路为准。

STM32 参数在 `Core/Inc/sensor_config.h`。ADC 默认参考电压为 3300mV，按 10/43 分压比还原并显示原电压。原工程 HSE 为 25MHz，实物晶振不同时须先调整时钟配置。

## 功能

- MPU6050：±2g、±250°/s、50Hz；显示三轴加速度（G）、三轴角速度（D/S）和芯片温度。上电时保持飞控板水平、静止约 3 秒，先收集 128 个稳定样本：AX/AY 和 GX/GY/GZ 扣除静止均值，AZ 扣除偏移后保留原方向的 ±1G。若板子移动或倾斜过大，重新收集，不会用运动数据校准；校准期间 OLED 显示 `IMU CALIBRATING`。一旦校准完成，偏移量在本次上电期间固定，运动时不会继续追零。陀螺仪偏移超过 20°/s 时，OLED 标题显示 `MPU BIAS HIGH`，提醒检查传感器。
- BMP388：读取出厂校准系数，进行温度及压力补偿；25Hz，压力 8 倍过采样；显示 hPa 和摄氏度。
- GPS：中断接收、1024 字节环形缓冲，在主循环解析具有正确校验和的 GGA；支持 GP/GN 等 talker；显示定位质量、卫星数和经纬度。不配置 GPS 模块本身，须启用 NMEA GGA 输出并匹配波特率。
- OLED：每 200ms 更新当前页数据，不再自动切页；页面由 SW5 四位拨码选择。SWITCH1（PA6）为 bit0、SWITCH2（PA7）为 bit1、SWITCH3（PA8）为 bit2、SWITCH4（PB9）为 bit3，开关接通为低电平。拨码 `0000` 显示 MPU，`0001` 显示 BMP388/ADC，`0010` 显示 GPS，`0011` 显示姿态角；其他编码保持当前页面。拨码编码稳定 30ms 后生效。KEY1/KEY2/KEY3 不再参与页面控制。
- 拨码选择表（编码按 `SWITCH4 SWITCH3 SWITCH2 SWITCH1` 从左到右书写）：全部断开 `0000` = MPU；仅 SWITCH1 接通 `0001` = BMP388/ADC；仅 SWITCH2 接通 `0010` = GPS；SWITCH1 与 SWITCH2 同时接通 `0011` = 姿态；其他组合保持上一页面。
- 姿态解算：校准后的 MPU6050 六轴读数先按安装方向转为机体坐标（X 向机头、Y 向左、Z 向上），用陀螺仪积分四元数，并在加速度合量为 0.8–1.2G 时用重力方向修正横滚和俯仰。已确认传感器 **-X 指向机头**；上电水平校准时根据 Z 轴重力符号识别 PCB 哪一面朝上。姿态页显示 ROLL（左侧抬起为正）、PITCH（机头抬起为正）、YAW（向右转为正），单位度。YAW 以启动时方向为 0°，没有磁力计绝对航向，静止零偏会造成缓慢漂移。
- BMP388 启动后先对 32 个有效压力样本求基准平均值 `P0`，期间高度保持 0m；之后使用 `H=44330×(1-(P/P0)^0.190294957)` 计算相对高度并显示。压力先经过 1/8 新样本的一阶低通，高度再经过 1/4 新样本的一阶低通，只抑制噪声，不对高度做死区或自动归零处理。传感器断线时高度页显示离线，重新恢复后重新建立本次上电的 `P0`。
- MPU6050 的加速度、角速度和温度在校准后使用 1/4 新样本的一阶低通；ADC 原码使用 1/4 新样本的一阶低通后再按 10/43 分压比换算。
- ADC 页面按用户确认的 10/43 分压比还原原始电压：`Vsource = VPA1 × 43 / 10`。`BAT` 显示还原后的电压，`ADC RAW` 仍显示12位原始码；这里假设 ADC 输入电压确实是原电压的 10/43。
- 传感器通信失败立即清除有效标记，500ms 没有新采样也判为离线；每 2 秒尝试重新初始化。GPS 3 秒没有有效 GGA 显示超时，无定位 GGA 显示等待定位。
- ESP32 建立密码保护的 `QUAD-CONTROL` 热点。手机连接后访问 `http://192.168.4.1/`，在网页上控制偏航、油门、俯仰和横滚；默认密码见 [手机遥控说明](esp32_oled/README.md)。ESP32 将手机指令编码为 QCTRL v1，通过 GPIO17→F411 PA3 的 UART 发送。F411 在接收中断中校验 CRC，并由 `ControlApp_GetLatest()` 提供最新指令；超过 250ms 未收到有效帧即返回无效。当前 F411 尚未实现电机 PWM 与姿态混控，控制帧只被接收和保存。
- OLED 默认显示手机热点与连接状态；网页可切换 OLED 到 F411 传感器页。手机发送控制时显示四路控制原码和帧序号；250ms 无指令则显示 `PHONE LINK LOST`，不显示旧控制量。F411 的 SW5 拨码仍负责选择传感器页内容。
- ESP32 上电显示手机控制等待页；STM32 有效显示帧中断超过 2 秒时，传感器页清除旧数据并提示链路断开；OLED 掉线后每 2 秒重新探测。ESP32 不再使用 NRF24。

这是传感器联调程序，主循环有短时间的阻塞 I²C、串口发送和初始化延时，尚不是飞控实时控制调度。代码没有启动电机 PWM 或姿态控制。

## 构建和烧录

### PC 上位机查看 MPU6050

仓库提供只读浏览器上位机 `tools/fc_monitor.py`，会从现有 OLED 帧解析 AX/AY/AZ、GX/GY/GZ 并画实时趋势，不需要修改 F411 固件，也不会向飞控发送数据。接线时使用 **3.3V UART 电平**的 USB-TTL：适配器 RX 接 F411 PA2（USART2_TX），GND 接飞控 GND，适配器 TX 不接。ESP32 的 RX 可继续接在 PA2 上。

```sh
python3 -m pip install pyserial
python3 tools/fc_monitor.py --list
python3 tools/fc_monitor.py --port /dev/ttyUSB0
```

打开终端输出的 `http://127.0.0.1:8765`。工具只在 OLED 当前为 MPU 页（拨码 `0000`）时显示六轴曲线。若串口是 `/dev/ttyACM0`，将命令里的设备名替换即可。Linux 用户若无串口权限，可将用户加入 `dialout` 组后重新登录。

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

烧录目标配置请求 1800kHz SWD，并使用 OpenOCD 的错误级别日志；这样不会显示 ST-Link 将 2000kHz 调整到 1800kHz 的信息提示。若需要调试日志，可手动去掉 Makefile 中的 `-d0`。

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

串口设备路径按实物修改。`menuconfig → Quad phone controller` 可修改手机热点名称和密码；`Quad sensor display` 可修改 OLED 引脚和 STM32 链路串口参数。完整使用方法见 [手机遥控说明](esp32_oled/README.md)。该目录是独立固件；现有 `test/wifi_test` 的 Wi-Fi/OTA 实验程序没有合并进来。

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

已通过协议与姿态算法测试、27 个 ARM C 文件检查，以及 GCC 完整固件构建；额外检查所有中断向量、初始栈地址、复位入口、GPS/SysTick 中断绑定、内存布局和未解析符号。GCC 构建生成的 ELF/HEX/BIN 可用于烧录。当前电脑有 ESP-IDF 源码，但缺少其 Python 虚拟环境；尚未完成 ESP32 构建、IAR 构建或实物测试。

上板验证：
1. 上电后 OLED 显示手机热点等待页；手机连接热点、打开 `http://192.168.4.1/`、按“开始控制”。网页将 OLED 切换为传感器页后，用 SW5 的 `0000`、`0001`、`0010`、`0011` 分别选择四页；若一直等待 STM32，核对 PA2→GPIO18 和共地。
2. 拆下桨叶，水平静置上电，等待约 3 秒完成 IMU 校准；姿态页应接近 0°/0°/0°。抬起机头时 PITCH 应增大，左侧机臂抬起时 ROLL 应增大；从上方看向右转时 YAW 应增大。若方向相反，核对 MPU 安装方向与机头定义。
3. 平放 MPU 时，加速度合量应接近 1G，静止角速度应接近零（存在零偏）；翻转或转动后数值应变化。
4. BMP 应显示所在地合理气压和芯片温度；GPS 在室外获得定位后显示有效坐标。
5. 断开单个传感器，确认对应页显示离线，拨码切换后其他页面数据仍更新；重新接入后确认恢复。
6. 停止 STM32，确认 OLED 提示链路断开；复位后确认恢复。
7. 用万用表对照 PA1 电压，再根据实测 VREF 和真实分压比扩展电池电压换算。

## 协议与依据

`Common/display_link.h` 是两端共用协议：2 字节 QD 帧头、1 字节版本 1、8×21 字节 ASCII 行数据、2 字节小端 CRC16-CCITT-FALSE，共 173 字节。CRC 覆盖帧头、版本和文本，接收端校验后整帧替换；丢字节或损坏后滑动寻找下一帧。当前发送显示文本，如后续需要 ESP32 数值计算，可新增协议版本传输结构化测量值。

寄存器和协议核对资料：
- [TDK MPU6050 register map](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf)
- [Bosch BMP388 datasheet，校准与补偿见附录](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp388-ds001.pdf)
- [ESP-IDF I²C master API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2c.html)
- [ESP-IDF UART API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/uart.html)
- [u-blox NMEA GGA 字段说明](https://content.u-blox.com/sites/default/files/documents/u-blox-F9-HPG-1.32_InterfaceDescription_UBX-22008968.pdf)
