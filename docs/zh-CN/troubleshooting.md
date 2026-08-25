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
