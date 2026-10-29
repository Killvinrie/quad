# 树莓派 OTA 服务部署记录

已部署到 `kevin@pi.local`，当前局域网 IP 为 `192.168.31.228`。

- 根目录：`/home/kevin/quad-ota`
- 固件目录：`/home/kevin/quad-ota/public`
- 发布工具：`/home/kevin/quad-ota/bin/publish.py`
- 服务：`quad-ota.service`，systemd 系统服务，以 `kevin` 用户运行，已开启自启
- ESP32 manifest 地址：`http://192.168.31.228:8000/manifest.json`
- 当前发布版本：1

服务仅依赖系统 Python 3。未修改 SSH 登录配置、路由器设置或防火墙。
HTTP 对外目录只包含 manifest 和 A/B 固件；首次烧录的 factory.hex 不对外提供。
IP 目前由 DHCP 分配，变化后需要同步 ESP32 配置；可在路由器中为树莓派保留此地址。

## 发布下一版

在当前电脑的仓库根目录执行，版本号递增。先上传到 incoming，发布工具校验两份文件后再原子替换 public 下的 manifest，无须重启服务：

```sh
make -C software banks boot
python3 tools/ota_release.py --version 2
scp software/build/release/manifest.json software/build/release/quad-2-*.bin kevin@pi.local:quad-ota/incoming/
ssh kevin@pi.local 'python3 ~/quad-ota/bin/publish.py ~/quad-ota/incoming'
```

工具拒绝哈希不匹配、错误槽地址、降低版本，以及同一个版本号对应不同固件。历史固件保留，避免发布切换时正在下载旧 manifest 的设备找不到文件。重复发布完全相同的版本是幂等的。

如果本机 SSH 报系统 ssh_config 权限错误，可给 ssh/scp 添加 `-F /dev/null`，绕过本机系统配置。密码通过终端交互输入，不写入文件。

## 服务检查

```sh
ssh kevin@pi.local 'systemctl status quad-ota.service --no-pager'
ssh kevin@pi.local 'journalctl -u quad-ota.service -n 30 --no-pager'
```

重启服务：

```sh
ssh -t kevin@pi.local 'sudo systemctl restart quad-ota.service'
```

重新安装服务时，仓库中的 `quad-ota.service` 含固定用户名和路径；换用户或目录需先修改。以 sudo 安装到 `/etc/systemd/system/quad-ota.service`，执行 `sudo systemctl daemon-reload` 和 `sudo systemctl enable --now quad-ota.service`。

已验证 active/running、enabled、树莓派本机 HTTP 200，以及当前电脑绕过代理后的局域网 HTTP 下载、两份固件大小和 SHA256。未重启整台树莓派，也未连接 ESP32/STM32 实物验证升级。此 HTTP 服务面向可信局域网。
