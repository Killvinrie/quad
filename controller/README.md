# 遥控器—ESP32—F411 控制链

`13遥控原理图.pdf` 的主控为 STM32F103C8T6。已生成三段代码：

1. `controller/stm32f103/`：F103 读取四路摇杆 ADC 和 8 个按键，20 ms 发送一次控制帧；同时发送到 NRF24L01 和 UART1。
2. `software/esp32_oled/main/nrf24_esp32.c`：ESP32-S3 通过 NRF24L01 接收控制帧，并从现有 UART1（GPIO17 TX、GPIO18 RX）转发到 F411 USART2（PA3 RX、PA2 TX）。ESP32 仍然负责 SSD1306 显示。
3. `software/Core/Src/control_app.c`：F411 在 USART2 接收中断中校验控制帧，保存最新的摇杆和按键状态，供后续飞控控制/电机混控调用。

## 控制帧

帧名为 QCTRL v1，长度 20 字节，适用于 NRF24L01 固定载荷和 UART：

| 字节 | 内容 |
|---:|---|
| 0～2 | `0x51 0x43 0x01`，帧头和版本 |
| 3 | 序号，8 位回绕 |
| 4～11 | YAW、THR、PITCH、ROLL，4 个小端 16 位 ADC 值（0～4095） |
| 12～13 | KEY1～KEY8，bit0～bit7，按下为 1 |
| 14 | flags，当前为 0 |
| 15～16 | 电池电压 mV；原理图没有 VBAT ADC，当前为 0 |
| 17 | 保留，0 |
| 18～19 | CRC16-CCITT，小端，覆盖字节 0～17 |

F411 超过 250 ms 没收到有效帧时，`ControlApp_GetLatest()` 返回无效，飞控应进入失控保护。当前项目还没有电机 PWM 和姿态混控，因此收到的命令只保存，不会直接驱动电机。

## ESP32-S3 接收端默认引脚

遥控器 PDF 没有 ESP32 接收板的连接图，因此接收端采用可通过 `menuconfig` 修改的默认值：NRF24 `SCK=GPIO12`、`MOSI=GPIO11`、`MISO=GPIO13`、`CSN=GPIO7`、`CE=GPIO4`。这些引脚避开现有 OLED（GPIO5/6）和 F411 UART（GPIO17/18），实际 PCB 不同只需修改 `menuconfig` 的 `Quad control NRF24 receiver`。

NRF24 两端固定使用：地址 `QDRC1`、频道 76、1 Mbps、2 字节 CRC、0 dBm。NRF24 必须使用稳定 3.3 V，并在模块旁增加 4.7～47 uF 去耦电容。

## 联调顺序

1. 在仓库根目录执行 `make -C controller/stm32f103 -j4` 编译独立 GCC 工程；连接 ST-Link 后执行 `make -C controller/stm32f103 flash` 烧录，不需要 CubeMX。
2. 观察 NRF24 供电和 UART1 控制帧。
3. 在 ESP32 工程执行 `idf.py menuconfig`，设置 NRF24 实际引脚，然后编译烧录。
4. F411 保持 USART2 与 ESP32 交叉连接：ESP32 TX→F411 PA3，F411 PA2→ESP32 RX，共地。
5. 通过 `ControlApp_GetLatest(&command, HAL_GetTick())` 读取控制量；先做失控超时，再接入电机混控。
