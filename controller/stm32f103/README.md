# STM32F103C8T6 遥控器固件

此目录对应 `controller/13遥控原理图.pdf`，是可独立编译的 STM32F103C8T6 工程。`Core/Src/board.c` 直接初始化寄存器，并提供现有应用使用的最小 HAL 兼容接口；启动文件、链接脚本和 Makefile 已包含，**不需要 CubeMX 生成工程**。使用片内 HSI 8 MHz，不依赖外部晶振。

## 原理图引脚

| 功能 | F103 引脚 | 配置 |
|---|---|---|
| YAW/THR/PITCH/ROLL | PA0/PA1/PA2/PA3 | ADC1 channel 0/1/2/3 |
| NRF24 SCK/MISO/MOSI | PB13/PB14/PB15 | SPI2 |
| NRF24 CE/CSN/IRQ | PA11/PB12/PA8 | CE、CSN 为 GPIO；IRQ 预留轮询 |
| UART1 TX/RX | PA9/PA10 | 115200 8-N-1，控制帧有线输出 |
| KEY1/KEY2 | PA15/PB11 | GPIO 输入上拉，低电平按下 |
| KEY3/KEY4 | PC14/PC15 | GPIO 输入上拉，低电平按下 |
| KEY5/KEY6 | PA6/PA7 | GPIO 输入上拉，低电平按下 |
| KEY7/KEY8 | PB0/PB1 | GPIO 输入上拉，低电平按下 |
| 蜂鸣器 | PA12 | GPIO 输出，当前预留 |
| AT24C02 SCL/SDA | PB6/PB7 | I2C1，当前预留参数存储 |

四路 ADC 原始范围为 0～4095，顺序是 `yaw, throttle, pitch, roll`。按键 bit0～bit7 对应 KEY1～KEY8，按下为 1；按键状态连续两个 20ms 周期一致后才更新，约 40ms 软件消抖。主循环每 20 ms 发送一次相同的 QCTRL v1 帧到 NRF24L01，同时通过 UART1 发送一份，便于台架有线联调。

## 编译和烧录

```sh
make -C controller/stm32f103 -j4
make -C controller/stm32f103 flash
```

需要 `arm-none-eabi-gcc`、`make`，烧录需要 ST-Link 和 OpenOCD。编译生成 `build/controller.elf`、`.hex`、`.bin`；`flash` 会通过 SWD 写入和校验，不能在未连接实物时执行。链接目标为 64 KiB Flash、20 KiB RAM，按 STM32F103C8T6 标称容量配置。烧录前确认 BOOT0 为低电平。

当前遥控板通过 ST-Link 报告的 SW-DP ID 是 `0x2ba01477`，而 OpenOCD 的 `stm32f1x.cfg` 默认要求 `0x1ba01477`。Makefile 在加载目标脚本前设置**精确匹配**的 `CPUTAPID=0x2ba01477`，并以 1000 kHz SWD 连接。此 ID 不足以单独确认芯片品牌或 Flash 容量；如换成报告 `0x1ba01477` 的板子，可执行 `make -C controller/stm32f103 flash CPUTAPID=0x1ba01477`，不必修改系统 OpenOCD 文件。

程序把 HSI 配为 8 MHz 系统时钟，SysTick 为 1 ms；ADC1 用 4 MHz 时钟逐通道采样，SPI2 为 2 MHz Mode 0，USART1 为 115200 8N1。8 个按键启用上拉，NRF24 CE 初始低电平、CSN 初始高电平。关闭 JTAG 但保留 PA13/PA14 的 SWD，使 PA15 可作 KEY1。PC14/PC15 分别接 KEY3/KEY4（已按图纸核对并修正旧引脚模板）。

NRF24 供电必须是稳定的 3.3 V，并在模块 VCC/GND 旁放置 4.7～47 uF 电容。遥控器发射地址、频道和速率必须与 ESP32 接收端保持一致。
