# 全新WSL2构建验证

公开Release前必须在可销毁的Ubuntu 22.04 WSL2发行版中完成两轮独立构建。

## 固定规则

- 临时发行版名称为`AirLink-Build-Verify`；
- 使用普通用户`builder`编译，不使用root编译；
- 只从`https://github.com/LX-DMT/AirLink.git`克隆源码；
- 不复制当前工程、host-tools、Buildroot下载缓存、`install/`、`out/`、
  旧镜像或旧RootFS；
- 逐字执行`make doctor`、`make bootstrap`、`make release`和
  `make verify`；
- 任意失败后都注销临时发行版，重新创建后才能再次声明纯净验证；
- 只允许在构建结束后把证据复制出来，不能复制构建输入进去。

## Windows自动验证脚本

先下载官方Ubuntu 22.04 WSL RootFS，然后以管理员身份打开PowerShell：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\windows\verify-clean-wsl.ps1 `
  -RootfsTar C:\path\ubuntu-jammy-wsl-amd64.rootfs.tar.gz `
  -RepositoryUrl https://github.com/LX-DMT/AirLink.git `
  -InstallRoot D:\AirLink-WSL
```

如果Windows代理只监听`127.0.0.1`，且WSL提示无法镜像localhost代理，先在
WSL中执行`ip route`查看Windows主机网关，再向脚本传入代理。例如Windows代理
端口为7897、WSL网关为`172.18.48.1`时增加：

```powershell
-ProxyUrl http://172.18.48.1:7897
```

代理只用于apt、Git克隆和`make bootstrap`下载；验证证据只记录不含密码的代理
端点。

脚本会检查GitHub HTTPS地址、导入全新WSL2、创建`builder`、执行README固定
命令、把证据和本次准确生成的发布`.img`复制到
`airlink-build-validation\`，然后注销临时发行版。`-InstallRoot`可省略；建议
指定至少有80GB可用空间的非系统盘，避免占满C盘。完整PowerShell/WSL控制台
输出会保存为`clean-wsl-console.log`。
`-KeepOnSuccess`只用于排查问题，不能把保留环境当作第二轮纯净验证。

同时记录RootFS哈希：

```powershell
Get-FileHash C:\path\ubuntu-jammy-wsl-amd64.rootfs.tar.gz -Algorithm SHA256
```

## 两轮验证

1. 推送Git跟踪的源码快照，在全新发行版完成第一次构建；修复缺文件、绝对路径和
   未说明警告后，销毁发行版；
2. 推送修正后的commit，用原始RootFS重新创建发行版，再次从GitHub克隆并执行
   四条固定命令。

第二轮生成的准确镜像还必须通过[发布流程](release-process.md)中的实机验收，
才可以创建正式Release。
