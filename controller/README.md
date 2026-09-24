# 旧 STM32F103 遥控器工程（已停用）

`controller/13遥控原理图.pdf` 和 `controller/stm32f103/` 保留早期 NRF24 遥控器设计及其 GCC 工程，用于资料归档。当前 ESP32 固件已改为 [手机 Wi-Fi 遥控](../software/esp32_oled/README.md)，**不再编译 NRF24 接收驱动**；无需再烧录 F103 遥控器或连接 NRF24。

F411 的 QCTRL v1 串口接收协议未改，ESP32 现在直接把手机网页的四路控制值编码为 20 字节控制帧发送给 F411。F411 已增加拆桨台架用的四路同油门输出；详情见[电机说明](../software/MOTORS.md)。
