# AirLink镜像结构

正式镜像固定为1694499328字节。

| 分区 | 起始扇区 | 扇区数 | 文件系统 | 内容 |
|---|---:|---:|---|---|
| Boot | 1 | 32768 | FAT16 | fip.bin、boot.sd、usb.host、ver、board |
| RootFS | 32769 | 3276800 | ext4 | Linux用户态、驱动、AirLink服务 |

`fip.bin`中的`BLCP_2ND`保存C906L R27P固件，运行地址为
`0x8fe00000`。`boot.sd`为FIT镜像，包含Linux Kernel、ramdisk和DTB。

Linux DTB关闭SPI0、I2C4和SARADC。SDIO设备树保留25MHz基础配置，AIC8800
BSP完成高速协商后请求50MHz，SG2002实际运行约46.875MHz。
