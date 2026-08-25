# Release发布流程

公开版本为V2.0.0，内部构建号为R27.6.6.23，正式标签为Airlink-V2.0.0。
只有两轮可销毁GitHub克隆构建和第二轮准确镜像实机验收均通过后才允许发布。

## 1. 冻结与备份

1. 确认所有发布源码已进入Git且工作区干净；
2. 在接入旧仓库历史前创建本地Git bundle；
3. 保留Airlink-V1.0.0标签和现有Git历史；
4. 候选源码先推送到release/v2.0.0-validation，不创建V2标签或Release。

## 2. 第一轮可销毁WSL验证

创建全新的Ubuntu 22.04 WSL2发行版AirLink-Build-Verify，以普通builder用户从
GitHub克隆验证分支并执行：

    make doctor
    make bootstrap
    make release
    make verify

不得复制host-tools、下载缓存、旧镜像、旧RootFS或当前构建输出。任意失败后必须
注销并重新创建发行版。

## 3. 第二轮默认分支验证

第一轮通过后，将main快进到已验证提交。销毁并重新创建WSL，然后逐字执行
README命令：

    git clone https://github.com/LX-DMT/AirLink.git
    cd AirLink
    make doctor
    make bootstrap
    make release
    make verify

验证报告必须记录最终Git提交，源码工作区变化数必须为0。

## 4. Release附件

第二轮纯净构建只公开以下附件：

    Airlink-V2.0.0.img
    SHA256SUMS
    build-info.txt
    manifest.json
    SBOM.spdx.json
    build-validation-report.md

原始镜像不得进入Git历史。构建环境、资源占用、压缩构建日志、ELF依赖、
RootFS体积、启动服务和FIP报告作为本地验证证据保存。

## 5. 实机验收

烧录第二轮准确镜像，完成冷启动10次、手机配网和重新配网、有线/无线切换20次、
VirtualHere 1GiB传输、CH347四种模式各切换10次，并回归屏幕、触摸、ADC和
屏保。确认SDIO实际约46.875MHz，无SDIO CRC/timeout、DWC2 reset及SSH、
Telnet、adbd监听。

源码或二进制发生任何变化后，原实机验收结果立即失效。

## 6. 正式发布

实机通过后：

1. 创建带注释标签Airlink-V2.0.0；
2. 创建同名GitHub Release；
3. 上传六个规定附件；
4. 重新下载附件并执行sha256sum -c SHA256SUMS；
5. 下载后的镜像完成最终冒烟测试后再公开。

Release说明必须标明VirtualHere与AIC8800固件属于遵循单独再分发条件的第三方
组件。
