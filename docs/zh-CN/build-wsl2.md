# WSL2完整构建教程

本文只描述经过验证的Ubuntu 22.04构建路径。不要混用其他SDK、旧镜像、旧RootFS
或开发机工具链。仓库不使用Git子模块，所有SG2002与AirLink源码都在同一次克隆中。

## 1. 硬件和系统要求

- Windows 10 22H2或Windows 11；
- WSL2 Ubuntu 22.04；
- 最低8GB可用内存，建议16GB；
- WSL Linux磁盘至少80GB可用空间；
- 首次构建需要稳定网络；
- 源码必须放在`/home/<用户名>`，不要放在`/mnt/c`。

构建会下载约881MB的官方host-tools归档，并由Buildroot下载带哈希校验的第三方
源码。完整SDK和RootFS会占用大量临时空间。

## 2. 安装WSL2

以管理员身份打开PowerShell：

```powershell
wsl --install -d Ubuntu-22.04
wsl --set-version Ubuntu-22.04 2
wsl --list --verbose
```

正常输出中`Ubuntu-22.04`的`VERSION`必须为`2`。第一次打开Ubuntu时，
按提示创建普通用户和密码。后续`make bootstrap`会要求输入这个密码执行sudo。

如果已经有其他Ubuntu发行版，不要在Ubuntu 20.04或24.04中编译本版本。

## 3. 克隆完整源码

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates git make python3

cd ~
git clone https://github.com/LX-DMT/AirLink.git
cd AirLink
```

不要执行`git submodule update`，也不要从其他目录复制`host-tools`、
`install`、Kernel、FIP或RootFS。

可先确认目录：

```bash
pwd
test -f Makefile
test -f airlink/c906l/build.sh
test -f airlink/linux/airlinkd.c
test -f linux_5.10/Makefile
```

## 4. 环境检查：make doctor

```bash
make doctor
```

该步骤检查：

- Ubuntu版本和WSL内核；
- 内存与磁盘；
- 必要源码目录；
- 无Git子模块/补丁恢复链；
- 无开发机绝对路径、私钥和超过GitHub限制的发布文件；
- VirtualHere专有二进制SHA256；
- SPI0、I2C4和SARADC的C906L资源归属。

正常输出示例：

```text
AirLink source tree verification: PASS
AirLink doctor: PASS
  Ubuntu: 22.04
  RAM: 15946 MiB
  free disk: 900 GiB
```

此时尚未安装工具链，因此不要使用`make doctor --strict`。

## 5. 安装依赖：make bootstrap

```bash
make bootstrap
```

该命令自动执行：

1. `apt-get update`；
2. 安装编译、镜像、设备树、文件系统和静态检查工具；
3. 下载锁定的SG2002官方`host-tools.tar.gz`；
4. 校验SHA256；
5. 解压到仓库内`host-tools/`；
6. 检查RISC-V musl和裸机编译器；
7. 再次执行严格doctor。

下载支持断点续传和重试。正常结束：

```text
AirLink source tree verification: PASS
AirLink doctor: PASS
AirLink bootstrap: PASS
```

工具链输入锁定在`versions.lock`。不要把`host-tools/`提交到Git，也不要把
它链接到其他开发目录。

## 6. 完整构建：make release

```bash
make release
```

不要在另一个终端同时执行第二个`make release`。构建阶段依次为：

1. 再次执行严格doctor；
2. 编译并测试C906L；
3. 编译并测试`airlinkd`和`airlinkctl`；
4. 清理并编译U-Boot、OpenSBI、Kernel、DTB、osdrv和AIC8800；
5. 编译middleware、ramdisk和Buildroot RootFS；
6. 从源码生成原始FIP和`boot.sd`；
7. 组装Release RootFS并删除无关用户态；
8. 执行ELF动态库依赖闭包检查；
9. 将新C906L写入FIP的`BLCP_2ND`；
10. 创建MBR、FAT16和ext4分区；
11. 生成1.58GiB镜像和zstd压缩包；
12. 从最终镜像读回关键组件并验证。

首次运行会由Buildroot下载固定版本第三方源码，通常需要一到数小时。完整控制台
输出保存在：

```text
out/logs/release-build.log
out/logs/sdk-build.log
```

最终正常输出包含：

```text
AirLink components: PASS
AirLink SG2002 SDK build: PASS
AirLink release image assembled:
AirLink release verification: PASS
AirLink validation evidence: PASS
AirLink complete release build: PASS
```

## 7. 独立复验：make verify

```bash
make verify
```

该命令不重新编译，而是重新检查最终镜像：

- 镜像总大小和MBR分区；
- FAT16中的`fip.bin`、`boot.sd`、`usb.host`、`ver`和`board`；
- FIP组件与C906L；
- FIT中的Kernel、ramdisk和DTB；
- DTB资源归属；
- ext4一致性；
- `airlinkd`、`airlinkctl`、VirtualHere和AIC8800模块读回哈希；
- Release中没有SSH、Telnet和adbd；
- IPC、ELF依赖、SBOM、manifest和SHA256。

必须看到：

```text
AirLink release verification: PASS
AirLink validation evidence: PASS
```

## 8. 输出文件

```text
out/release/Airlink-V2.0.0.img
out/release/SHA256SUMS
out/release/build-info.txt
out/release/manifest.json
out/release/SBOM.spdx.json
out/release/build-validation-report.md
out/release/build-environment.txt
out/release/build-resource.txt
out/release/build.log.zst
out/release/deleted-files.txt
out/release/elf-dependencies.txt
out/release/rootfs-size.txt
out/release/startup-services.txt
out/release/fip-components.json
```

检查哈希：

```bash
cd out/release
sha256sum -c SHA256SUMS
```

## 9. 单独编译AirLink组件

开发者可执行：

```bash
make components
```

输出：

```text
out/components/c906l/r26-lvgl.bin
out/components/linux/airlinkd
out/components/linux/airlinkctl
out/components/SHA256SUMS
```

这不能替代`make release`，因为正式镜像还必须重新构建Kernel、DTB、ramdisk、
RootFS、驱动和FIP。

## 10. 已知无害厂商警告

SG2002 SDK包含上游警告，完整保留在`build.log.zst`中。以下警告在退出码为0、
组件哈希和镜像验证均通过时可接受：

- `FLASH_SIZE_SHRINK set more than once`：板级defconfig重复赋相同构建策略；
- OpenSSL 3.0 deprecated API：旧U-Boot主机工具调用兼容接口；
- 部分驱动`unused-function`、`format`和C90声明警告；
- 部分递归Makefile的`jobserver unavailable`提示；
- 不参与AirLink运行的厂商摄像头/多媒体样例警告；
- 旧DTS中未使用摄像头/多媒体节点的前置条件警告；
- 禁用手册生成时e2fsprogs文档目标显示的已忽略Error 1；
- FAT小写卷标兼容性警告。

以下情况不允许忽略：

- 顶层命令退出码非0；
- `undefined reference`、找不到头文件/库、DTB语法错误；
- AIC8800、Kernel、FIP或RootFS输出缺失；
- `AirLink ... PASS`未出现；
- `make verify`失败。

## 11. 常见问题

### host-tools下载中断

直接重新运行：

```bash
make bootstrap
```

脚本会从仓库内`.cache/`断点续传并重新校验SHA256。

### Buildroot下载失败

确认网络和DNS恢复，然后重新执行`make release`。Buildroot按固定版本和hash
下载，不要手工放入来源不明的压缩包。若需要提交Issue，请附上失败URL和日志末尾
200行。

### 磁盘不足

```bash
df -h ~
du -sh host-tools buildroot/output linux_5.10/build ramdisk install out
```

释放空间后重新从干净克隆构建。不要把构建目录移动到`/mnt/c`再用符号链接绕过。

### 权限错误

源码应归普通用户所有：

```bash
sudo chown -R "$USER":"$USER" ~/AirLink
```

不要用`sudo make release`。

### CRLF或脚本无法执行

```bash
git config --global core.autocrlf input
git reset --hard
```

重新克隆通常比批量修改脚本换行更安全。

### 查看失败位置

```bash
tail -200 out/logs/release-build.log
grep -nE 'Error|ERROR|FAILED|No such file|undefined reference' \
  out/logs/release-build.log | tail -100
```

不要复制其他工程的`install/`、旧镜像或旧RootFS掩盖缺失输入。

## 12. 清理

```bash
make clean
```

该命令删除AirLink生成的`out/`。下一次`make release`本身仍会执行SDK
`clean_all`并从源码重建关键组件。

## 13. 烧录前

只有同时满足以下条件才应烧录验收：

- `make release`成功；
- `make verify`成功；
- `SHA256SUMS`校验成功；
- `build-validation-report.md`对应当前commit；
- 镜像来自全新WSL克隆，而不是旧开发目录。

两轮强制可销毁环境构建见[全新WSL2构建验证](clean-wsl-validation.md)。
