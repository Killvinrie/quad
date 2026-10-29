# 树莓派 → ESP32-S3 → STM32F411 A/B OTA

本实现升级的是 F411 飞控固件。ESP32 固件自身仍通过 USB 烧录。
F411CE 的 512KiB Flash 是单硬件 bank；这里使用两个独立链接的应用槽，Bootloader 选择向量表并跳转，不复制正在执行的应用。当前 GCC 应用约 56KiB，每槽限制 128KiB，超限由链接器拒绝。

| 地址 | 大小 | 用途 |
| --- | --- | --- |
| 0x08000000 | 32KiB | Bootloader，扇区 0、1 |
| 0x08008000 | 16KiB | 元数据副本 1，扇区 2 |
| 0x0800C000 | 16KiB | 元数据副本 2，扇区 3 |
| 0x08010000 | 192KiB | 预留，扇区 4、5 |
| 0x08040000 | 128KiB | 应用 A，扇区 6 |
| 0x08060000 | 128KiB | 应用 B，扇区 7 |

## 首次安装

在仓库根目录执行，需安装 arm-none-eabi 工具链：

```sh
make -C software banks boot
python3 tools/ota_release.py --version 1
```

输出包括 `software/build/A/quad.bin`、`software/build/B/quad.bin`、`software/build/boot/boot.bin`，以及 `software/build/release/` 中的发布文件、manifest 和首次安装用 `factory.hex`。
A/B bin 的链接地址不同，不能互换。发布工具检查复位向量是否属于对应槽。

首次安装必须接 ST-Link，并拆桨。以下命令写入 Bootloader、两份初始元数据和 A 槽应用，重新初始化升级状态；只用于首次安装或人工恢复，后续版本通过 OTA 更新：

```sh
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "adapter speed 1800; program software/build/release/factory.hex verify reset exit"
```

不要直接烧录旧的 0x08000000 应用，也不要只烧录一个新应用后就尝试启动。原 `make flash` 已改成提示，避免沿用旧流程。当前 OTA 构建以 GCC Makefile 为准；IAR 工程未迁移，不能使用旧 IAR 独立应用烧录步骤。CubeMX 再生成代码时需保留 ota_app 的初始化后轮询、USART2 字节接入与向量表设置。

## 树莓派发布版本

树莓派与 ESP32 连接同一个路由器。把整个 release 目录传到树莓派，例如 `~/quad-ota`，运行：

```sh
python3 -m http.server 8000 --bind 0.0.0.0 --directory ~/quad-ota
```

确认电脑可访问 `http://树莓派IP:8000/manifest.json`。该服务不需要额外 Python 包；运行期间保持终端或服务进程存活。

后续修改飞控代码后重新编译并增加整数版本号：

```sh
make -C software banks boot
python3 tools/ota_release.py --version 2
```

先把发布目录中的两份 `quad-2-*.bin` 传入树莓派服务目录，再替换 `manifest.json`；版本号必须递增。文件名含版本、槽位和 SHA256 前缀，不覆盖旧版本的二进制。同一目录本地发布时脚本最后以原子替换方式发布 manifest；跨机器复制时也要最后替换 manifest。

manifest 结构如下，实际 SHA256 和长度由脚本生成：

```json
{
  "target": "stm32f411ce",
  "version": 2,
  "banks": {
    "A": {"file": "quad-2-A-xxxx.bin", "size": 56148, "sha256": "64位小写十六进制"},
    "B": {"file": "quad-2-B-yyyy.bin", "size": 56148, "sha256": "64位小写十六进制"}
  }
}
```

## ESP32 配置与首次烧录

使用 ESP-IDF 6.1。此机器现有环境可用 `source ~/.espressif/tools/activate_idf_v6.1.sh` 激活；其他机器用自己的 export.sh。

```sh
cd software/esp32_oled
idf.py menuconfig
```

在 `Quad STM32 A/B OTA` 中设置：

- 开启 `Enable automatic STM32 updates from Raspberry Pi`。
- 填写路由器 SSID 和密码。
- 填写树莓派 `http://实际IP:8000/manifest.json`。

保持串口为 115200 8N1。接线沿用 ESP GPIO17 TX → F411 PA3 RX、F411 PA2 TX → ESP GPIO18 RX，共地。无需新增 BOOT0 或复位控制线。

```sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

组件管理器会安装锁定的 `espressif/cjson 1.7.19~2`。OTA 默认关闭，以免未配置路由器时改变已有控制流程。开启后使用 AP+STA：保留手机控制热点，同时加入路由器。AP+STA 的热点信道随 STA 连接调整，首次联网期间手机可能需要重连。

## 升级过程和恢复

ESP32 默认每 5 秒检查一次 manifest（可在 menuconfig 的 `Version check interval (seconds)` 修改），读取 F411 的当前槽和已确认版本。F411 已解锁时拒绝查询/进入升级；ESP32 不会为了升级主动中断飞行控制。

发现更高版本后，ESP32 下载非当前槽对应的固件到 RAM（最多 128KiB），核对长度和 SHA256。下载期间继续提供控制；下载后再次查询状态，请求 `ENTER`。F411 在主循环确认未解锁、没有待确认版本且已完成首次运行确认后，回复并通过 RTC 备份寄存器标志复位进入 Bootloader。

ESP32 收到进入升级的确认后屏蔽网页遥控。Bootloader 擦除非当前槽，按 252 字节数据块写入。每个请求带序号和 CRC32；ESP32 超时重发，Bootloader 对上一条重复请求重发 ACK。收满后核对整体 CRC32、栈指针、复位向量及地址边界，再提交 pending 元数据并复位。

Bootloader 在首次试运行前持久化 tried 标记。新应用的主循环运行约 5 秒后确认新槽；确认前发生看门狗复位、人工复位或断电，下次启动会撤销 pending 并启动旧槽。这里的自检保证初始化完成并持续轮询，不代表姿态算法或飞行性能已经验证。

元数据交替写入两个独立擦除扇区，CRC 最后写入。擦写期间掉电保留上一个有效副本。固件下载或非当前槽写入失败不会覆盖当前槽；传输中断后可重试整个下载，不支持字节级断点续传。Bootloader 无传输会话时等待 30 秒后回到有效当前槽；会话中断后会继续等待下一轮 ESP32 重试，人工断电重启也会返回旧槽。两份元数据都无效时停留 Bootloader，需 ST-Link 重装 factory.hex。

日志 `Version N confirmed` 表示已通过查询确认新槽。若收到 `New version not confirmed`，检查 STM32 状态；不要把下载成功当成安装成功。更新失败会在后续检查时重试，因此坏版本应从服务器撤下或发布修正后的更高版本。

该版本用于可信局域网，HTTP 和 manifest 尚未签名。SHA256/CRC 检测传输损坏，不提供发布者认证。请勿直接暴露到公网。固件暂存于 RAM，内存不足时放弃本轮升级并保留当前应用。

## 已完成的验证与上板检查

```sh
python3 -m unittest discover -s test/ota -v
```

主机测试执行实际 C 串口解析器和元数据存储代码：标准 CRC 向量、噪声/损坏帧恢复、两个元数据副本每一字节位置写入中断、应用槽边界、首次试运行/复位回滚/确认，以及发布包 HEX 校验和。STM32 A、B、Bootloader 和 ESP32 OTA 开/关配置均已编译。

尚未连接实物联调。拆桨上板后依次检查 A→B、B→A、下载断网、写入中断电、试运行确认前复位、错误哈希拒绝和已解锁时拒绝升级。确认电机在整个 Bootloader 阶段保持停机后，再将其纳入日常使用。

## 当前树莓派部署

已部署到 `kevin@pi.local`，manifest 地址为 `http://192.168.31.228:8000/manifest.json`，服务 `quad-ota.service` 已开启自启。后续发布和维护命令见 [部署记录](../../tools/ota_pi/README.md)。

## 检查 ESP32 与树莓派通信

ESP32 串口会分别打印路由器连接、DHCP 地址、HTTP manifest 成功，以及 STM32 串口状态。
重新烧录最新 ESP32 固件并运行 `idf.py -p 实际串口 monitor`，以 `Router connected; ESP32 IP:` 核对 ESP32 地址；看到 `Raspberry Pi communication OK: HTTP 200, version=N` 即表示已从树莓派读取并解析版本信息，不依赖 STM32 的应答成功。
`STM32 UART timeout` 表示树莓派请求成功后，STM32 查询阶段没有应答；`No newer firmware` 表示两段通信都正常，但版本不需要升级。

树莓派实时日志可通过 `ssh -t kevin@pi.local 'journalctl -u quad-ota.service -f'` 查看，核对请求来源 IP 与 ESP32 的 DHCP 地址。仅电脑能下载文件不能证明 ESP32 已联网。Wi-Fi 断线日志包含 reason 数值，HTTP 错误包含地址及状态码，串口重试失败包含请求类型。

## 检查频率和升级进度

默认每轮结束后等待 5 秒再检查版本；网络请求超时、STM32 应答重试或正在升级时，实际间隔会更长。获取 DHCP 地址的检查间隔为 1 秒，未联网时每约 5 秒输出一次等待提示。
下载会输出开始和校验完成耗时，串口写入阶段约每秒输出一次百分比、字节数，完成时输出耗时。擦除前、镜像校验前以及重启确认前均有阶段提示，串口应答超时每次重试也会打印。
这只改变检查频率和进度可见性，STM32 串口仍为 115200，应用启动后的 10 秒确认等待保持不变；ESP32 需要重新通过 USB 烧录才能使用这些改动。
