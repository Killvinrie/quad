# STM32F103C8T6 遥控器固件

此目录对应 `controller/13遥控原理图.pdf`。工程使用 STM32Cube HAL，建议在 CubeMX 中新建 STM32F103C8Tx 工程后加入 `Core/Src/controller_app.c`、`Core/Src/nrf24l01.c` 和对应头文件；`Core/Inc/main.h` 是本图纸的引脚模板。

## 原理图引脚

| 功能 | F103 引脚 | 配置 |
|---|---|---|
| YAW/THR/PITCH/ROLL | PA0/PA1/PA2/PA3 | ADC1 channel 0/1/2/3 |
| NRF24 SCK/MISO/MOSI | PB13/PB14/PB15 | SPI2 |
| NRF24 CE/CSN/IRQ | PA11/PB12/PA8 | CE、CSN 为 GPIO；IRQ 预留轮询 |
| UART1 TX/RX | PA9/PA10 | 115200 8-N-1，控制帧有线输出 |
| KEY1/KEY2 | PA15/PB11 | GPIO 输入上拉，低电平按下 |
| KEY3/KEY4 | PC13/PC14 | GPIO 输入上拉，低电平按下 |
| KEY5/KEY6 | PA6/PA7 | GPIO 输入上拉，低电平按下 |
| KEY7/KEY8 | PB0/PB1 | GPIO 输入上拉，低电平按下 |
| 蜂鸣器 | PA12 | GPIO 输出，当前预留 |
| AT24C02 SCL/SDA | PB6/PB7 | I2C1，当前预留参数存储 |

四路 ADC 原始范围为 0～4095，顺序是 `yaw, throttle, pitch, roll`。按键 bit0～bit7 对应 KEY1～KEY8，按下为 1；按键状态连续两个 20ms 周期一致后才更新，约 40ms 软件消抖。主循环每 20 ms 发送一次相同的 QCTRL v1 帧到 NRF24L01，同时通过 UART1 发送一份，便于台架有线联调。

## CubeMX 配置

- HSE/系统时钟按实际晶振配置；ADC1 扫描或单通道软件切换均可，代码使用软件切换四个通道。
- SPI2：主机、8 位、Mode 0、软件 NSS，时钟不超过 8 MHz。
- USART1：115200、8 位、无校验、1 停止位。
- 8 个按键 GPIO 使用内部上拉；NRF24 CE/CSN 初始输出高，CE 在初始化时拉低。
- PA13/PA14 保留 SWD；不要把 PA15/JTAG 复用为调试口，关闭 JTAG 只保留 SWD 后 KEY1 才可用。

NRF24 供电必须是稳定的 3.3 V，并在模块 VCC/GND 旁放置 4.7～47 uF 电容。遥控器发射地址、频道和速率必须与 ESP32 接收端保持一致。
