# Realtek RTL8761BU A728 在 macOS Tahoe 上连接 MI RC：完整排障记录

记录时间：2026-10-18 至 2026-11-15（UTC+8）。对象：一台 x86_64 OpenCore 测试机、Realtek USB 蓝牙 `0BDA:A728`、小米蓝牙语音遥控器 MI RC（应用识别为 RC003）、SayAll 1.9.21 build 174、豆包输入法。实测补丁目标为 **macOS 26.7.3 build 25G416 / Darwin 25.6.0**。

## 结果摘要

1. 原先 MI RC 建立 BLE 链路后，macOS 有时停在 `LE_READY_WAIT_FOR_PHY`，SayAll 超时，未进入有效 GATT 通信。初始化抓包证实主机在 `LE Set Event Mask` 里关闭了 PHY Update Complete 和 Data Length Change。
2. **RealtekEventMask 0.2.3** 在目标 USB 控制 OUT 请求提交前对 opcode `0x2001` 做 `mask |= 0x0840`。运行时证实前后值 `1f 00 00 00 00 00 00 00 → 5f 08 00 00 00 00 00 00`，异步 USB 提交和完成均为 0，实际传输 11 字节，控制器的 HCI Command Complete 状态为 0。随后抓到真实的 PHY 和 DLE 事件、ATT 双向通信、普通按键和语音。
3. 建连修复后仍有实际链路超时。关 JBL 音箱后仍复现；将 A728 从原 USB 位置移至机箱顶部后，短时观察里未再出现连续掉线，豆包语音已能出字。USB 位置是有效的对照变量；具体射频或硬件机制、长期稳定性、其他系统构建仍未验证。
4. 最终补丁作为隔离候选完成实机验证，并保留无补丁回滚基线。源码及构建细节见[最终补丁](RealtekEventMask/README.md)，精简证据见[证据索引](EVIDENCE.md)。

## 1. 早期硬件识别与驱动候选

### 2026-10-18：确认目标控制器

- 初期曾读到 Realtek USB 蓝牙 `0BDA:8771`，但设备会随物理拔插消失；不能把它与后来稳定复现的目标适配器混为一谈。
- 拔除适配器时系统曾报告 `BCM_4350C2 / UART / Off / NULL / firmware v0 c0`。仅凭该系统标签不足以证明 USB 总线上存在独立的 Broadcom 控制器。
- 检查 IOUSB 树排除了 Realtek `0BDA:C811` 802.11ac 网卡。再次插入目标蓝牙棒后，实际枚举为 Realtek `0BDA:A728`，IORegistry 有 bluetoothd 接管证据，系统报告第三方控制器已开启。之后实验均以 **A728** 为准。

### 2026-11-14：排除旧猜测并验证固件加载器

- 回看 BlueToolFixup 的 `-btlfxallowanyaddr`，其作用是处理控制器地址匹配检查。现场没有内置与外置控制器地址相同的证据，因此未使用该参数。
- 从 [RTLBluetoothFirmware](https://github.com/bennetzakaria/RTLBluetoothFirmware/tree/b83e314500be5b88b48cd3516b9c0d46e3e64e2d) 获取源码，修改其 `RTLBluetoothFirmware_UB500` personality 的 USB VID/PID 为 `3034/42792`。
- 使用 Xcode Command Line Tools 和 macOS 26.5 SDK 编译；上游固件下载遇到 HTTP 403 后改用 [linux-firmware 官方仓库](https://gitlab.com/kernel-firmware/linux-firmware/-/tree/main/rtl_bt) 对应固件，并核对固件头与哈希。该加载器测试后 BLE 超时仍复现，因而继续检查 HCI 初始化。

## 2. 从 PacketLogger 空窗口到初始化抓包

- PacketLogger 初次显示 `0 total`。左栏的 LE Command/Event 名称是优先级预设，不能当作已捕获事件。
- 在本机备份 `/Library/Preferences/com.apple.MobileBluetooth.debug.plist`（若原本不存在则记录该状态）。仅开启 `HCITraces.StackDebugEnabled` 和 `HCITraces.HCIFileTraces`，通过 `killall -30 bluetoothd` 通知重读。未启用 RawAudioTrace 或 HCISkipAuth。
- 先以 LightBlue 扫描，并确认受保护目录 `/private/var/root/Library/Logs/Bluetooth` 中的 `bluetoothd-hci-*.pklg` 含真实 HCI 记录，再记录一次 Heart Rate 连接和目标 MI RC/SayAll 的复现。系统 HCI 文件与 `bluetoothd` 统一日志按时间和连接句柄对齐。HCI 抓包可包含配对材料，公开部分只使用重建的命令参数、事件字段和 SHA-256。
- Heart Rate 当作对照；MI RC 是验收目标。对比 Mac Air 的正常路径与测试机的就绪日志。早期目标遥控器收到成功的 Remote Features 回报，但在多次失败中等不到 PHY 就绪。约 7.4–7.5 秒后应用取消，主机发 Disconnect，断开原因 `0x16`；这和后来设备已可用时的 `0x08` 超时是不同阶段。
- 只读检查当前构建的 bluetoothd，PHY 回调在连接对象有效时推进 PHY 就绪状态。没有就绪回调的那轮未进入正常 ATT 初始化。个别旧抓包里有一条通知，不能据此判断完成了 GATT 初始化。连接句柄会复用，分析每次需要把句柄与最近的成功建链关联。
- 参考 [Intel Issue #514](https://github.com/OpenIntelWireless/IntelBluetoothFirmware/issues/514)、[PR #515](https://github.com/OpenIntelWireless/IntelBluetoothFirmware/pull/515/files) 与 [Intel 补丁源码](https://github.com/if-lensman/Hackintosh-MacPro7-1-EFI/tree/main/patches/IntelBTPatcher)。它们提供 Tahoe 等待 PHY/DLE 的线索，Intel 方案会重复 Remote Features 并改写事件，不能把它的结果当成 A728 实测。本次没有安装 IntelBTPatcher 作为 A728 修复。
- 为确定事件为何缺席，从物理 USB 重新枚举之前开始记录，覆盖 Reset、能力查询、事件订阅、默认 PHY 和一次真实连接。尝试确认独立 USB 捕获能力；本机没有确认可用的 USB 捕获接口，`usbmon0` 不存在。PacketLogger 是 HCI 层记录，不能同时作为线上 USB 的独立抓包。

## 3. 原始初始化数据与假设

| 操作 | 观察 |
|---|---|
| Reset `0x0c03` | 成功 |
| HCI Set Event Mask `0x0c01` | `ff ff ff ff ff ff ff 3f`，状态 0，LE Meta bit 61 已开启 |
| LE Set Event Mask `0x2001` | `1f 00 00 00 00 00 00 00`，状态 0，低 5 位开启，bit 6、bit 11 关闭 |
| LE Features | `bd 5f 66 00 00 00 00 00`，包括 DLE 和 LE 2M |
| Supported Commands | 从 0 编号的 octet 35 为 `f8`，涉及 Read PHY、Set Default PHY、Set PHY |
| LE Set Default PHY `0x2031` | `00 02 02`，状态 0 |
| 后续命令与事件 | 无掩码覆盖；未看到 Set PHY `0x2032`、Read PHY `0x2030`、Set Data Length `0x2022`，也没有真实 `0x3e/0x0c`、`0x3e/0x07` |

当时的证据足以确认主机订阅与等待条件不一致。没有看到 Set PHY 和完成事件不能单独证明控制器损坏；默认 PHY 不等于对每个连接显式发 Set PHY。先验证打开订阅能否得到真实事件，全程不注入 HCI 成功事件。

## 4. 五个补丁版本和回滚

| 版本 | 实现 | 实测结果 |
|---|---|---|
| 0.1.0 | 在 USB 请求路径试开 bit 11 | kext 确实加载；缺少路由、命中和传输证据。PacketLogger 仍见 `1f`，7 次成功建链回报但未进入正常 ATT，不能判定 bit 11 单独无效。 |
| 0.2.0 | 换到 `IOUSBHostDevice::internalDeviceRequest`，OR `0x0840`，加入 IORegistry 前后值和计数 | 已加载，`RouteState=4`，命中 0。把内核 `osversion` 当 macOS build 比较的保护条件失败。登录采集器读取 Documents 脚本也被系统拒绝。 |
| 0.2.1 | 移除错误运行时字符串比较，仍以 Darwin 25.6.0 限定；采集器迁到用户 Library | 该候选启动后键鼠无法使用；未找到对应 kernel panic，bluetoothd 用户态异常不足以证明根因。撤回该候选、隔离故障 kext 并重新验证配置。 |
| 0.2.2 | 先检查包头再读设备状态，改用已有 IORegistry VID/PID，移除每个普通 USB 请求上的描述符查询及 OUT 原地写，加入入口机器码检查；仅同步路径使用独立缓冲区 | 真实目标请求出现，两次 11 字节候选里的最后一次是异步，因此没有改写。其他 kext 的 Lilu 回调还会污染路由状态，造成真实命中与 `RouteState=3/Error=2` 同时出现。 |
| **0.2.3** | 按 USB kext 索引过滤回调，补齐异步完成转发与缓冲区生命周期，单独记录 USB 提交、完成和长度 | 键鼠正常。目标异步命令命中，掩码、实际传输、HCI 完成、真实事件、ATT、按键和语音均有证据。 |

每轮测试前保留可回滚基线或前一候选。0.2.3 的源码、bundle 与配置经过构建、签名、掩码测试、plist、`ocvalidate` 和文件一致性核验。各候选均由用户手动选择启动。

## 5. 0.2.3 的运行证据

- 01:53:44 启动，确认版本为 0.2.3。IORegistry `RouteState=2`、`RouteError=0`，USB setup `21 00 00 00 00 00 0b 00`；`MaskBefore=1f00000000000000`、`MaskAfter=5f08000000000000`、`MaskWriteVerified=Yes`。首轮 `PatchedRequests=1`；异步 USB 返回和完成各为 0，实际长度 11。
- 01:54:00.661 PacketLogger 记录主机侧 `0x2001` 原值，01:54:00.662 收到真实 `Command Complete`、状态 0。与 Hook 前后值合并解读：HCI 追踪位置看到的是改写前的主机命令；Hook 证明缓冲区被替换并经 USB 传输完成。没有独立线缆抓包。
- 01:56:41.466 MI RC 建链，41.510 上报真实 DLE、双向 251 字节，41.839 上报真实 PHY Update Complete、状态成功、双向 2M。随后交换 ATT MTU、读取/写入、收到通知，应用侧报告 `connected:1`。未见补丁增加 `Set PHY`、`Set Data Length` 或重复 `Read Remote Features`。
- 此实验一次开启两个事件位，未隔离出每一个位是否单独足够。不能把 0.1.0 的缺证据当作有效的单变量实测。

## 6. SayAll、虚拟音频和豆包

- 蓝牙接通、收到普通 HID 键、接到 ATVV 语音、音频到达虚拟设备、豆包将文字提交，是五个不同的观察点。
- 01:56 有语音 47 批、11,280 个采样送完。SayAll 未观察到外部工具提交文本，无法检查豆包内部实际麦克风。后来诊断仍显示这组计数，但它属于旧尝试，不能用来证明当次音频就绪。
- 02:00 系统底层多次重连，SayAll 停在搜索。02:04:52 抓包和应用日志都收到 TV 键按下/松开，向导由此通过遥控器步骤；其时未出现新的应用 `BLE READY` 或 ATVV 流。
- 完全退出、重开 SayAll，02:06:18 恢复 `BLE READY`。音频页仍卡住。MiRemoteV 已绑定，但引擎反复报告配置变化、`engine_running=false`。当时 JBL Flip 6 是系统默认播放输出（44.1 kHz），显示器 P326MV MAX 和 MiRemoteV 2ch 为 48 kHz。
- 临时把默认播放输出从 JBL 切到 P326MV MAX，SayAll 的语音输出仍是 MiRemoteV 2ch，系统提示音输出留在 JBL。02:07:56 引擎恢复并 `ready=true`。这是一次有效对照，未证明采样率本身就是根因。
- 02:08:18 语音键触发、音频引擎健康；约 0.84 秒后控制器 HCI `0x08` 超时，零采样。SayAll 随断线释放音频，随后返回音频未就绪。这里音频提示是断线后的结果。
- 02:11 两次近距离测试仍遇到超时；02:12:03 一次语音持续约 2.2 秒，33,840 个采样全部播放，未见当次掉线；向导记录输入窗口非前台，`input_target_not_ready`。
- 豆包出现“识别优化”后消失。02:13:58 的一次语音只有约 0.82 秒就掉线，只收到 1,920 个采样。没有证据表明豆包内部完成识别。对照方法是让文本编辑的插入光标保持在前台，确认豆包输入设备；必要时用 QuickTime 的 MiRemoteV 录音电平排除“虚拟麦克风没有收到音频”。参见 [SayAll 豆包兼容说明](https://github.com/HD838A/remote-mic-app/blob/main/Resources/豆包输入法兼容说明.en.md)。
- 02:15:41，一次约 1.8 秒、24,000 采样的语音结束；02:15:42 掉线，但向导在 02:15:44 已判定语音通过。重连后进入常用按键页，又发生一次掉线。02:16:32–33 收到左键、确定、右键，02:16:34 向导进入 complete。用户随后报告功能可用，但无法持续保持连接。

## 7. 连接稳定性：关闭 JBL 和移动 USB 口

| 条件 | 观测 |
|---|---|
| 原 USB 口，JBL 连着 | 02:15:42–02:18:01 的抓包有 6 次 `0x08`。连接间隔常从 30 ms 更新到 15 ms，监督超时时间 720 ms。普通待机或语音阶段都超时。 |
| 原 USB 口，JBL 已断开 | 抓包中 JBL 于 02:20:55 主机端断开。02:21:11–43，MI RC 11 次断开，7 次 `0x08`，4 次 `0x3e`。关闭 JBL 仍复现。 |
| A728 移到机箱顶部，JBL 保持断开 | USB 位置从 `0x14600000` 变为 `0x14830000`；补丁计数增至 2，第二次 USB 完成仍为 0。02:23:46 SayAll 恢复 BLE READY。约 2.3 秒及 4.6 秒语音完整播放；用户发来的语音输入文字可读。 |
| 顶部 USB 口继续观察 | 到 02:25:49 无新掉线或重连失败，四次有采样并全部播放。一次 180 ms 短触发零采样，未伴随掉线，之后两次有采样。02:29:42 再查应用日志，此窗口 8 次语音事件，7 次有采样，无新断线或重连失败。 |

### 延长观察与音频配置复测

- 顶部 USB 口从 02:23:43 到 02:42:18 的 HCI 抓包中，02:37:32.871 仅见一次 MI RC 断开（`0x22`），随后控制器约 0.73 秒内重连，约 3.85 秒后 SayAll 恢复 `BLE READY`，约 7.64 秒后音频就绪。未再出现前一端口的 `0x08/0x3e` 循环。JBL 在 02:26:41 重连，说明较长的观察窗口也覆盖了 JBL 同时连接的情况。此窗口 SayAll 15 次语音事件中 13 次有采样。
- 重启后的另一轮测试，JBL 成为系统默认播放输出（44.1 kHz），MiRemoteV 处于 48 kHz。12:43 的一次语音收到了 170 批音频但全部入队失败，日志持续显示音频引擎未就绪。切到显示器后引擎恢复，随后按需求把 **MiRemoteV 调成 44.1 kHz** 并切回 **JBL 默认播放输出**。12:45:56 的实测中 SayAll 正常绑定 MiRemoteV，收到 90 批、21,600 采样，零入队失败，缓冲全部播放完。Core Audio 当时只为这条 JBL 蓝牙连接列出 44.1 kHz。该对照证实两者可同时工作，采样率设置的跨重启保持尚未验证，不能仅凭此测试断定所有音频引擎异常的根因。

端口对照支持“USB 位置影响可用性”的结论。机箱遮挡、端口拓扑或 2.4 GHz 干扰各自的贡献还没有测量。保持当前端口使用即可继续积累观察；如果复发，先记时刻，分别对照 HCI 断开原因、语音样本和豆包文本提交。

## 8. 可重复的排查顺序

1. 使用 `sw_vers`、`uname -r`、`ioreg -p IOUSB -l -w0` 核对系统构建、Darwin、实际 VID/PID、USB 位置。用 `kmutil showloaded` 证实当前启动确实加载预期 kext。
2. 采集初始化窗口；用 HCI Command 出参和 Command Complete 确认有效掩码。查每次 Reset 后是否重新设掩码，区分设备能力、主机请求、事件上报三件事。
3. 对测试版用 `ioreg -r -c RealtekEventMask -l -w0` 验证路由、匹配、USB 提交、USB 完成与字节数。用 PKLG 单独核验 HCI 完成和后续事件。
4. 对目标连接按事件顺序核对 LE Connection Complete、Data Length Change、PHY Update Complete、ATT/GATT、输入报告和 ATVV 数据。对 `0x08` 与 `0x3e` 单独计数，避免把应用的主动取消混进去。
5. 按键和语音各测一次，核对 SayAll 日志里的 BLE READY、音频引擎、ATVV 样本、虚拟音频播放和外部文字提交。把不同连接的同一 handle、旧计数和本轮失败分开。
6. 调试采集结束后按原始备份恢复配置：原本没有 HCITraces 时只删除新增的键；原本有配置则恢复原值。本记录未核实机器已经关掉 HCI 追踪。

## 9. 发布与兼容范围

这个仓库记录一次特定机器的实验。0.2.3 是本次最终测试候选，使用私有 USB 内核符号及入口检查。当前只证实 A728 / x86_64 / 25G416 的这套组合。原始固件或系统文件、HCI/SMP/语音抓包及设备唯一标识仅保留在本机。公开证据使用重建字段和摘要。长期稳定、不同芯片、不同构建及双事件位最小需求均未验收。
