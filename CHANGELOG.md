# Changelog / 开发变更记录

> 这不是正式 release tag；项目还处于原型联调阶段。下面的 `v0.x` 是为了方便回顾而补的**开发里程碑编号**，提交 SHA 才是源码的唯一版本依据。
>
> These are development milestones, not formal release tags. The Git commit SHA remains the source of truth for every revision.

## Current status / 当前状态

- Phone-driven Web OTA is hardware-verified: an iPhone connected directly to the ESP32 SoftAP, uploaded the STM32 application, and completed the update without a PC-side sender.
- 手机 Web OTA 已通过实机验证：iPhone 直连 ESP32 SoftAP、上传 STM32 Application，并在不使用电脑发包工具的情况下完成升级。
- The Web update moved the target from firmware version 1 to 2; a follow-up Bluetooth `VERSION` query returned `FW Version: 2`.
- Web 升级将目标版本从 1 更新到 2；随后通过蓝牙 `VERSION` 查询得到 `FW Version: 2`。
- The full PC → Bluetooth SPP → ESP32 SPIFFS → STM32 bootloader → Flash/CRC → application path is now hardware-verified.
- PC → 蓝牙 SPP → ESP32 SPIFFS → STM32 Bootloader → Flash/CRC → Application 的完整路径现已通过实机验证。
- A 15,956-byte application image (`CRC32 0x3B274D7E`) completed OTA successfully, and the updated STM32 application replied `FW Version: 1` after reboot.
- 15,956 字节的应用镜像（`CRC32 0x3B274D7E`）已完成 OTA；重启后 STM32 新应用返回 `FW Version: 1`。
- All three firmware targets build, the protocol smoke test passes both the classic and all-byte CRC vectors, and the current hardware wiring is stable at USART1 9600 baud.
- 三端固件均可构建；协议烟测同时覆盖经典 CRC 向量和全字节向量；当前硬件接线在 USART1 9600 baud 下稳定工作。

## Milestones / 里程碑

### v0.11 — Phone-driven Web OTA / 手机独立完成 Web OTA

**Commit:** [`5b6ab60`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/5b6ab60)

- 中文：ESP32 新增常驻 SoftAP `STM32-OTA-Bridge` 和响应式 OTA 页面，iPhone 连接热点后访问 `192.168.4.1` 即可选择 Application `.bin`、填写版本并查看上传/写入状态。
- English: Added an always-available `STM32-OTA-Bridge` SoftAP and a responsive OTA page. An iPhone can connect to `192.168.4.1`, choose an application image, set its version, and follow upload/programming status.
- 中文：新增 `GET /api/status`、`POST /api/upload`、`POST /api/start`；上传过程流式写入 SPIFFS，不把完整固件放进 RAM。OTA 任务与蓝牙路径共用互斥锁，避免两个入口同时操作 STM32。
- English: Added `GET /api/status`, `POST /api/upload`, and `POST /api/start`. Uploads stream into SPIFFS instead of buffering a full image in RAM, while a shared mutex prevents concurrent Web/Bluetooth transfers.
- 中文：除大小和 CRC 外，ESP32 还检查 Cortex-M 初始栈、Thumb Reset Vector 和 `0x08002000..0x0800F800` Application 地址范围，防止误传 Bootloader 或无关固件。
- English: Beyond size and CRC checks, the bridge validates the Cortex-M stack pointer, Thumb reset vector, and the `0x08002000..0x0800F800` application range to reject bootloader or unrelated binaries.
- 验证 / Validation: iPhone 上传 15,956-byte `fw_v2.bin` 后页面完成 OTA；COM6 查询返回 `FW Version: 2`。ESP32 构建占用 RAM 65,152 B（19.9%）、Flash 1,349,653 B（73.6%）。 / The iPhone-uploaded 15,956-byte image completed OTA, and COM6 reported firmware version 2.

### v0.10 — Hardware-verified end-to-end OTA / 实机验证完整 OTA 闭环

**Commit:** [`646bda2`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/646bda2)

- 中文：把 PC→ESP32 暂存改成文本安全的 Base64 分块协议：`FW` 声明元数据，`DATA <offset>,<base64>` 逐块发送并等待偏移量 ACK，`VERIFY` 做暂存前检，最后 `SEND` 触发 STM32 OTA。丢 ACK 时允许同一块安全重试，SPIFFS 文件保持单次打开，减少重复打开/追加带来的不确定性。
- English: Reworked PC-to-ESP32 staging into a text-safe Base64 block protocol: `FW` declares metadata, `DATA <offset>,<base64>` sends acknowledged blocks, `VERIFY` performs the staging preflight, and `SEND` starts STM32 OTA. Lost ACKs can be retried safely, and SPIFFS remains open for the entire staging write.
- 中文：定位并修正 `shared/protocol.c` 与 ESP32 镜像里 4 个错误的 CRC-32 查表常量。经典 `123456789` 测试会漏掉该错误，因此新增 `0x00..0xFF` 全字节回归向量，期望值为 `0x29058C73`。
- English: Corrected four bad CRC-32 table constants in both protocol implementations. The classic `123456789` vector did not exercise those entries, so an `0x00..0xFF` regression vector with expected CRC `0x29058C73` was added.
- 验证 / Validation: 15,956-byte app image, CRC `0x3B274D7E`; ESP32 reported `STATUS: OTA complete!`; after reboot, `VERSION` returned `FW Version: 1`. / A 15,956-byte image with CRC `0x3B274D7E` completed OTA, and the rebooted STM32 reported firmware version 1.

### v0.9 — Deterministic full-OTA pipeline / 可确定时序的完整 OTA 链路

**Commit:** [`1898386`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/1898386f95054f242bc59570a229fa337a0dc316)

- 中文：修掉了几处会让真实 OTA 必然失败的代码问题：应用先写 OTA 配置并回复 `CMD_OTA_READY`，ESP32 再等待 Bootloader；1 KB 数据块连同 4 字节序号的 1028 字节负载被正式支持；9600 baud 下 ACK 超时改为 2 秒；Bootloader 不再在 `OTA_BEGIN` 时多擦一遍整个 App 区。
- English: Fixed several defects that would make real OTA fail: the application now persists the OTA request and replies with `CMD_OTA_READY` before the ESP32 waits for the bootloader; the protocol now accepts a 1028-byte OTA payload (4-byte sequence + 1 KB data); the 9600-baud ACK timeout is 2 s; and the bootloader no longer redundantly erases the entire app region at `OTA_BEGIN`.
- 中文：把 CRC 统一为标准 IEEE CRC-32，强制 Flash 擦写函数保留在 `.ramfunc` 并在启动时复制到 RAM；新增 `tools/bridge_ota.py` 自动走 Bluetooth COM → SPIFFS → STM32 的流程，以及 C 协议烟测。
- English: Standardized CRC handling on IEEE CRC-32, forced Flash-writing functions to remain in `.ramfunc` and copied them to RAM at startup, added `tools/bridge_ota.py` for Bluetooth COM → SPIFFS → STM32 transfer, and added a C protocol smoke test.
- 验证 / Validation: 三个 PlatformIO 目标已构建成功、协议烟测通过、新 STM32 Bootloader/App 已由 ST-Link 写入并 Verify；ESP32 刷写和完整 OTA 实机闭环仍待进行。 / All three PlatformIO targets build, the protocol smoke test passes, and the new STM32 bootloader/app were ST-Link programmed and verified; ESP32 flashing and full OTA hardware closure are still pending.

### v0.1 — Initial OTA system skeleton / 初始 OTA 系统骨架

**Commit:** [`6e1872e`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/6e1872e0634f72b148a94253b15cd8ad6c401eda)

- 中文：建立 STM32 Bootloader、FreeRTOS 应用、ESP32 网关、共享 UART 帧协议和 Python 发包工具的第一版工程骨架；定义了 Flash 分区、OTA 配置页和基本状态机。
- English: Created the first project skeleton: STM32 bootloader, FreeRTOS application, ESP32 gateway, shared UART framing protocol, and a Python sender. It also defined the Flash layout, OTA config page, and basic state machines.
- 验证边界 / Validation boundary: 架构和代码基线，不代表当时已完成硬件联调。 / Architectural baseline only; no end-to-end hardware claim.

### v0.2 — Project documentation and target architecture / 项目文档与目标架构

**Commit:** [`de46d69`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/de46d694931b2bfdd0d10ce80ee904d756e2ed29)

- 中文：补充完整项目框架、传感器/执行器扩展架构和 README，使项目的“最终目标形态”可读。
- English: Added the broader project framework, planned sensor/actuator architecture, and a richer README so the intended end state is documented.
- 注意 / Note: 这里包含设计目标，不能一律理解为已经接上硬件实现。 / Some items describe intended architecture, not already-integrated hardware.

### v0.3 — STM32 build-layout corrections / STM32 构建与布局修正

**Commit:** [`c7a211d`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/c7a211d0de722e9045e7aa0510f536f7bfe98607)

- 中文：修正 `BootConfig_t` 尺寸断言和 Flash 宏保护，并补上根目录 PlatformIO 配置，避免 STM32 编译和布局定义互相冲突。
- English: Fixed the `BootConfig_t` size assertion and Flash macro guards, and added the root PlatformIO configuration to prevent STM32 build/layout conflicts.

### v0.4 — Buildable FreeRTOS application / 可构建的 FreeRTOS 应用

**Commit:** [`5afecf0`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/5afecf01033f823a14e3dda4f878ed2cdbbbfb4f)

- 中文：把 FreeRTOS 内核和 PlatformIO 构建接起来，修正应用链接脚本与启动配置，应用固件第一次能作为独立目标构建。
- English: Integrated the FreeRTOS kernel with PlatformIO and corrected application linker/startup settings so the application could build as a standalone target.

### v0.5 — Usable ESP32 staging bridge / 可用的 ESP32 暂存网关

**Commit:** [`3cd7d6a`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/3cd7d6a5a40edf7abe8ddf903dcade387ce48e27)

- 中文：增加 SPIFFS 分区和蓝牙固件暂存流程；支持 `FW <version>,<crc>`、二进制文件推送、`SEND`、Wi-Fi 凭据保存及 `OTA <url>` 下载入口。
- English: Added a SPIFFS partition and Bluetooth firmware staging flow; introduced `FW <version>,<crc>`, binary file push, `SEND`, persistent Wi-Fi credentials, and the `OTA <url>` download entry point.
- 验证边界 / Validation boundary: 功能接口已具备，但当时还没有证明真实 STM32 Flash OTA 已跑通。 / The interface existed, but real STM32 Flash OTA was not yet proven.

### v0.5.1 — ESP-IDF source inclusion / ESP-IDF 源文件接入

**Commit:** [`60308cd`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/60308cda608a3fb43fffa8f623888d715e475b09)

- 中文：补齐 ESP-IDF/PlatformIO 的组件与源文件配置，使 ESP32 目标能正确带入共享协议实现。
- English: Added ESP-IDF/PlatformIO component and source configuration so the ESP32 target includes the shared protocol implementation correctly.

### v0.5.2 — Reproducible ESP-IDF configuration snapshot / 可复现的 ESP-IDF 配置快照

**Commit:** [`9fff0b8`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/9fff0b84942eb0b94bad1647e17edaa68dae7a81)

- 中文：提交由 ESP-IDF 生成的 `sdkconfig` 快照，固定当前开发板所需的蓝牙/构建配置。
- English: Committed the generated `sdkconfig` snapshot to preserve the Bluetooth and build configuration needed by the development board.

### v0.5.3 — ESP-IDF 6 Bluetooth API compatibility / ESP-IDF 6 蓝牙 API 兼容

**Commit:** [`34d24ce`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/34d24cec4a161f812431122b4085951ab2143892)

- 中文：适配 ESP-IDF 6 的 Classic Bluetooth/SPP 初始化 API，修复 UART 配置写法和 Wi-Fi 状态读取，使蓝牙服务真正可以启动。
- English: Adapted the bridge to ESP-IDF 6 Classic Bluetooth/SPP initialization APIs, corrected UART setup, and fixed Wi-Fi status handling so the Bluetooth service could actually start.

### v0.6 — ESP32 build green on the actual 2 MB board / 实际 2 MB ESP32 构建通过

**Commit:** [`abd9953`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/abd9953f41635c0dcc5aacff9e974efd887c87da)

- 中文：根据实物 ESP32 的 2 MB Flash 调整分区；修复 ESP-IDF 6 构建问题，并为 ESP32 复制共享协议实现，三个目标开始能稳定编译。
- English: Adjusted partitions for the physical ESP32's 2 MB Flash, fixed ESP-IDF 6 build issues, and added the ESP32 copy of the shared protocol implementation so the targets could compile reliably.

### v0.7 — Three-target build evidence / 三目标构建证据

**Commit:** [`f725239`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/f725239bea23fe606b883361214280aa9945e9d8)

- 中文：记录 Bootloader、STM32 应用、ESP32 Bridge 三个固件目标的构建结果、内存占用和已知风险。
- English: Recorded build results, memory usage, and known risks for the bootloader, STM32 application, and ESP32 bridge targets.

### v0.8 — Bluetooth-to-STM32 hardware bring-up / 蓝牙到 STM32 实机联调

**Commit:** [`860fc96`](https://github.com/finnyoun9/bluepill-ota-bootloader/commit/860fc96209a5a0f4053c98471b5677660212a2d2)

- 中文：这是当前最新、也是这次实际硬件排障的核心提交。把 STM32 链路统一到 `USART1 PA9/PA10 @ 9600`，增加兼容当前 ST-Link/OpenOCD 的烧录脚本，修复 ESP32 Classic SPP 配置、蓝牙文本命令分包重组和 UART 协议镜像问题。
- English: This is the latest and the key hardware bring-up commit. It standardized the STM32 link on `USART1 PA9/PA10 @ 9600`, added a ST-Link/OpenOCD-compatible uploader, and fixed ESP32 Classic SPP configuration, split text-command reassembly, and the UART protocol mirror.
- 实机结果 / Hardware evidence: PC Bluetooth `COM6` 连续 10 次 `VERSION` 均得到 `FW Version: 0`；逻辑分析仪也解码到了从 ESP32 发往 `PA10` 的有效帧。
- Remaining scope / 尚未覆盖: 这只证明控制链路能双向收发，**不等于**固件经蓝牙 OTA 到 STM32 已经完成。

## How to use this file / 如何使用这份记录

- 想看源码差异：点击上面的 SHA，或执行 `git show <sha>`。
- To inspect the code-level diff, open the SHA above or run `git show <sha>`.
- 想看当前可执行步骤：优先看 `README.md` 和 `docs/build-notes.md`；这里专门回答“某一版为什么改、改到了哪”。
- For current runnable steps, start with `README.md` and `docs/build-notes.md`; this file answers why a revision changed and what it actually reached.
