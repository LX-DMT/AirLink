# 常见问题

## Ubuntu版本不正确

只支持Ubuntu 22.04。Ubuntu 24.04中的工具和Python版本可能与SDK不兼容。

## 磁盘不足

执行`df -h ~`检查。源码、host-tools和完整构建建议预留80GB。不要将工程
移动到`/mnt/c`。

## host-tools下载失败

```bash
curl -I https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz
```

网络恢复后重新运行`make bootstrap`。

## 找不到C906L编译器

```bash
sudo apt update
sudo apt install gcc-riscv64-unknown-elf
make doctor
```

## SDK编译失败

查看：

```bash
less out/logs/sdk-build.log
```

不要复制其他工程的`install/`、Kernel或旧镜像绕过错误。提交问题时附上
日志末尾200行和`make doctor`输出。

## VirtualHere发现不到AirLink

确认电脑和AirLink处于同一局域网，路由器关闭访客隔离，并允许TCP 7575。
电脑连接5GHz通常可以避开部分路由器对2.4GHz客户端的隔离。

## WSL克隆GitHub或下载工具链速度极慢

Windows使用本机代理时，WSL2 NAT中的`127.0.0.1`不是Windows主机。先查看
默认网关，再设置代理：

```bash
ip route | grep default
export http_proxy=http://172.18.48.1:7897
export https_proxy=$http_proxy
git clone https://github.com/LX-DMT/AirLink.git
```

请按本机实际网关和代理端口替换示例值。不要把代理密码、Token或认证信息写入
仓库。
