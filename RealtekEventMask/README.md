# RealtekEventMask 0.2.3

本次实测的源码快照及构建脚本。它是 Lilu 插件，依赖 Lilu，不能替代 USB 固件加载器。适用证据仅覆盖 x86_64、macOS 26.7.3 / 25G416、Darwin 25.6.0、Realtek `0BDA:A728`。内核范围约束由 OpenCore 配置实施；代码额外检查目标函数的入口字节，并未精确比较系统构建号。

## 作用与界限

- Lilu 只在 IOUSBHostFamily 对应的 kext 加载索引上解析 `IOUSBHostDevice::internalDeviceRequest`。核对 25G416 读取的 x86_64 入口字节后建立调用路由，拒绝入口不符。
- 保留所有普通请求的直通路径。先筛控制 OUT setup、长度和设备 VID/PID，再读取缓冲区，最后要求完整 HCI 字节头为 `01 20 08`。匹配 setup 类型为 `0x20/0x21`，请求号为 `0x00/0xe0`，wValue/wIndex 为 0，wLength 为 11。
- 只置掩码参数 byte 0 的 `0x40` 与 byte 1 的 `0x08`。即 `mask |= 0x0000000000000840`，其他事件位保持原值。与固件、远端特征、默认 PHY、链路参数和 ATT 数据无关。
- 捕获到的目标命令走异步请求。代码从原始数据或可读 OUT 描述符中复制 11 字节，核对 opcode 后制作独立发送描述符。同步调用返回后释放；异步成功后在完成回调中转发原 owner、parameter、状态及长度，并释放自有资源。提交失败时清理自己的资源。没有增加 HCI 命令、伪造成功事件或写系统二进制。
- IORegistry 的 `MaskUSBReturn` 是 USB 提交，`MaskUSBCompletion` 是 USB 完成，控制器 HCI Command Complete 从追踪单独验证。计数是同一启动周期内累计值，最后一次的字符串或数据属性不能代替逐请求追踪。

## 构建

目录关系与实测脚本匹配：

```text
RealtekEventMask/
dependencies/Lilu-1.7.2/Lilu/
dependencies/MacKernelSDK-master/Headers/
```

Lilu 取自 [1.7.2 上游发布](https://github.com/acidanthera/Lilu/tree/1.7.2)，内核头来自 [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)。当时 SDK 头从 master 快照获取，未单独记录提交号。构建目录应在不会自动为 bundle 附加 Finder 元数据的位置。本仓库只包含本次候选的源文件；构建脚本通过外部依赖目录引用上游代码，不包含上游源代码或预编译 kext。

```bash
xcode-select -p
bash RealtekEventMask/build.sh
xcrun clang++ -std=c++14 -O2 RealtekEventMask/mask-test.cpp -o /tmp/rtlmask-unit
/tmp/rtlmask-unit
plutil -lint RealtekEventMask/RealtekEventMask.kext/Contents/Info.plist
codesign --verify --deep --strict RealtekEventMask/RealtekEventMask.kext
```

实测构建使用 CLT macOS 26.5 SDK，脚本目标 x86_64，Lilu 头会产生若干编译告警。掩码测试穷举受影响的两个字节的 65,536 种取值，验证保留其余字节、幂等以及拒绝错误头和短包。到此只完成构建与纯逻辑验证；USB 异步生命周期及键鼠仍需实机测试。受工具链、SDK 和签名差异影响，重建结果不承诺字节级相同。

## OpenCore 与回退

对于已准备好的独立测试 U 盘，测试项位于 `OC/Kexts/RealtekEventMask.kext`，在 `Kernel → Add` 中放在 Lilu 之后：

| 键 | 实测值 |
|---|---|
| BundlePath | RealtekEventMask.kext |
| ExecutablePath | Contents/MacOS/RealtekEventMask |
| PlistPath | Contents/Info.plist |
| Arch | x86_64 |
| Enabled | true |
| MinKernel / MaxKernel | 25.6.0 / 25.6.0 |

备份并保留一份不加载本插件的 EFI，使用匹配 OpenCore 版本的 ocvalidate。实测为 OpenCore 1.0.8。首次测试应能通过启动菜单选回原 EFI。已能启动时，可通过禁用该 Kernel/Add 条目或 boot arg `-rtlmaskoff` 关闭实验。在无法使用键鼠时直接从预先保留的启动路径恢复。0.2.1 曾实际出现键鼠不可用；0.2.3 已在该机验证键鼠正常。

实测先检查 `kmutil showloaded` 版本和 `ioreg -r -c RealtekEventMask -l -w0`。加载本身不能证明命中。预期状态为 `RouteState=2`、`RouteError=0`、`MaskWriteVerified=Yes`、`MaskAfter=5f08000000000000`、USB 完成 0、传输 11 字节，控制器成功返回 `0x2001`。最后分别测键鼠、普通遥控键、语音、待机重连。关于二进制及运行证据的哈希见 [证据索引](../EVIDENCE.md)。
