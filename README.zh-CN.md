**简体中文** | [English](README.md)

# AirLink SG2002

当前公开固件：**V2.0.0**，内部构建号为R27.6.6.23。

AirLink是基于SG2002的双模USB扩展与USB over IP终端。有线模式下电脑直接接管
USB Hub；无线模式下由Linux通过Wi-Fi和VirtualHere共享USB设备。C906L小核独立
运行圆屏、触摸、电池ADC和CH347控制，因此Linux启动或联网时界面仍可正常操作。

![AirLink](docs/assets/airlink-hero.jpg)

## 主要功能

- 有线USB Hub，并提供HDMI、网口和3.5mm耳机口；
- 2.4GHz/5GHz Wi-Fi，支持手机热点配网和Captive Portal；
- VirtualHere USB共享，并显示真实监听与电脑连接状态；
- CH347四种本地模式及当前模式引脚对照；
- C906L裸机LVGL圆屏UI，GC9A01 SPI实际46.875MHz；
- AIC8800 SDIO请求50MHz，SG2002实际约46.875MHz；
- IPC协议v1、ABI4，C906L为R27P，Linux为LN27。

## 下载与烧录

在仓库右侧的 **Releases** 页面下载：

```text
Airlink-V2.0.0.img
SHA256SUMS
manifest.json
```

在Linux或WSL中校验：

```bash
sha256sum -c SHA256SUMS
```

Windows可使用balenaEtcher、Rufus或Win32 Disk Imager直接写入下载的
`.img`。烧录会清空所选磁盘，请先确认TF卡盘符。

## Windows WSL2从源码编译

### 电脑要求

- Windows 10或Windows 11；
- WSL2 Ubuntu 22.04；
- 至少8GB内存，建议16GB；
- 至少80GB可用磁盘；
- 稳定网络，用于安装Ubuntu软件包和下载SG2002工具链。

不要把源码放在`/mnt/c`下编译，否则权限、大小写和文件性能可能导致失败。

### 第一步：安装WSL2

以管理员身份打开PowerShell：

```powershell
wsl --install -d Ubuntu-22.04
wsl --set-version Ubuntu-22.04 2
```

安装完成后启动Ubuntu 22.04，创建普通用户名和密码。

### 第二步：克隆源码

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates git make python3

cd ~
git clone https://github.com/LX-DMT/AirLink.git
cd AirLink
```

本仓库已经直接包含完整SG2002 SDK和AirLink源码，不需要初始化子模块，
也不需要下载或应用补丁。

### 第三步：检查环境

```bash
make doctor
```

正常结果：

```text
AirLink doctor: PASS
Ubuntu: 22.04
```

若提示磁盘或内存不足，应先解决，不要继续编译。

### 第四步：安装依赖

```bash
make bootstrap
```

该命令会安装apt依赖、下载SG2002官方host-tools并检查交叉编译器。安装过程中
会要求输入当前Ubuntu用户的sudo密码，完成后应显示：

```text
AirLink bootstrap: PASS
```

### 第五步：完整编译

```bash
make release
```

该命令会依次编译C906L、airlinkd、Kernel、DTB、ramdisk、RootFS、AIC8800
驱动和原始FIP，然后生成Release RootFS、替换BLCP_2ND并打包1.58GiB镜像。
首次编译通常需要一到数小时。

### 第六步：再次验证

```bash
make verify
```

最终必须看到：

```text
AirLink release verification: PASS
```

产物位于：

```text
out/release/Airlink-V2.0.0.img
out/release/SHA256SUMS
out/release/manifest.json
out/release/SBOM.spdx.json
out/release/build-info.txt
```

详细说明见[WSL2完整构建教程](docs/zh-CN/build-wsl2.md)。

## 首次使用

无线模式且没有保存Wi-Fi时，设备建立：

```text
热点：AirLink-XXXX
密码：12345678
地址：192.168.4.1
```

手机连接热点并选择目标Wi-Fi。若页面没有自动弹出，访问
`http://192.168.4.1/`。电脑安装VirtualHere Client并与AirLink处于同一
局域网后即可连接USB设备。

## 文档

- [完整使用说明](docs/zh-CN/user-guide.md)
- [软件架构](docs/zh-CN/architecture.md)
- [WSL2完整构建教程](docs/zh-CN/build-wsl2.md)
- [镜像结构](docs/zh-CN/image-layout.md)
- [常见问题](docs/zh-CN/troubleshooting.md)
- [全新WSL2构建验证](docs/zh-CN/clean-wsl-validation.md)
- [发布流程](docs/zh-CN/release-process.md)

## 许可证

AirLink自研源码及构建脚本使用Apache-2.0。Linux、U-Boot、Buildroot、
OpenSBI、LVGL、字体、驱动和厂商固件继续遵循各自原许可证。

VirtualHere为专有软件，不属于Apache-2.0开源范围，具体见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
