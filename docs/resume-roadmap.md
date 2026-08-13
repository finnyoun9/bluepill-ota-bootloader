# 简历主项目全局规划

> 目标岗位：STM32 / MCU / 智能硬件嵌入式软件工程师。
> 规划基线：2026-08-13 代码与硬件现状。只有完成验收并保留证据的内容才能写成“已实现”。

## 项目组合定位

| 顺序 | 项目 | 证明的能力 | 当前阶段 |
|---|---|---|---|
| 1 | STM32F103 + FreeRTOS + ESP32 智能环境终端 | Bootloader/OTA、RTOS、协议、无线网关、Web、本地 UI、软硬件联调 | 主链路已通，新 UI/控制开发中 |
| 2 | STM32F407VET6 最小系统板 | 原理图、PCB Layout、焊接、Bring-up、硬件故障定位 | PCB 已回板，待焊接和上电验证 |

两个项目刻意互补：项目 1 证明固件和系统集成深度，项目 2 证明硬件设计到实物验证的完整链路。

## 项目 1：智能环境终端

### 已完成且可用于简历

- 自定义 8 KB Bootloader，Application 从 `0x08002000` 运行并完成 VTOR 重定位。
- ESP32 通过 Bluetooth/Web 暂存固件，再以 UART 自定义帧协议向 STM32 分块升级。
- OTA 包含 CRC-32、序号 ACK、超时重试、镜像大小和 Cortex-M 向量表预检；真实硬件已完成升级闭环。
- FreeRTOS 五任务、队列、事件组、StreamBuffer、栈溢出/内存失败 Hook 和独立看门狗。
- AHT20、BMP280、BH1750、PIR、OLED、ST7789、EC11、按键、继电器、蜂鸣器和 WS2812B 的驱动与集成。
- ESP32 提供传感器 REST API、响应式 Web 仪表盘和执行器控制入口。
- 使用逻辑分析仪定位 WS2812B “DIN 有波形但第一颗灯珠未接收”，以 DOUT 为证据边界完成 bit-bang 时序校准。

### P0：先收口成可稳定演示版本

1. 完成当前未提交的 OLED 状态页、TFT 菜单、手动/自动灯光模式、网页亮度与中英文同步。
2. 实机逐项回归：TFT 旋钮和返回键、网页开关、继电器断电、手动亮度、自动调光、语言切换、重启后默认状态、Web OTA。
3. 记录最终引脚表、构建占用、操作视频和关键截图；提交并打 `v1.0` tag。
4. RAM 当前约 17.9 KB / 20 KB。启用真实 HighWaterMark 和最小 heap 统计，按测量结果缩减任务栈和 14 KB FreeRTOS heap，留下可量化余量。

验收标准：连续运行 24 h 无复位；网页/TFT 状态一致；执行 20 次控制循环和至少 3 次 OTA 均成功；保存串口日志、波形与版本号。

### P1：补可靠性和工程证据

1. 给共享协议增加主机端自动测试：CRC 向量、粘包/拆包、非法长度、损坏帧、重复/乱序数据。
2. 增加 GitHub Actions，自动构建 Bootloader、Application、ESP32，并运行协议测试。
3. 做 OTA 故障注入：中途断开 UART、错误 CRC、重复 chunk、ESP32 复位、掉电后旧 Application 是否仍可启动。
4. 将传感器失效、UART 超时、看门狗复位原因和 OTA 结果变成可查询的诊断状态。
5. 整理 3 个面试可讲的调试案例：WS2812B DOUT、UART/CRC、继电器供电链路。

验收标准：CI 全绿；故障注入结果表完整；每个错误路径都有预期响应和恢复方式；README 中“已验证”与“规划”严格分开。

### P2：只选一个增强方向

优先推荐 MQTT，而不是继续加传感器：ESP32 发布状态并订阅控制主题，Pi5 Mosquitto 保存时序数据和告警。这样能补齐端—网关—服务链路，同时复用现有硬件。

不建议在求职前同时做 WebSocket、原生 App、Node-RED、更多传感器和加湿器。它们会增加演示面，但不会显著提高 MCU 岗说服力。

### 明确不写或不夸大的内容

- 当前没有 DMA + IDLE 环形缓冲区，STM32 UART 是 RXNE 中断 + StreamBuffer。
- 当前没有 Modbus RTU、RS485、历史曲线和云端 MQTT 闭环。
- 当前 Bootloader 是单 Application 分区，不是 A/B 双分区自动回滚。
- TFT/网页同步的新代码在完成烧录回归和提交前只能写“开发中”。

## 项目 2：STM32F407VET6 最小系统板

### 焊接前设计复核

- 对照 STM32F407 datasheet 与 AN4488 复核所有 VDD/VSS、VDDA/VSSA、VREF+、VBAT、VCAP1/2、BOOT0、NRST、SWD 和 HSE 网络。
- 原图中 `VREF+` 连接需要重点确认；官方建议其连接 VDDA 或合法外部参考，并满足电压范围。
- 现有 AMS1117 周边只看到小容量电容，焊接前按实际 LDO 厂商 datasheet 核对输入/输出稳定电容；必要时飞线补电容。
- 检查 5 V、3.3 V、GND 是否短路，LDO 封装引脚与 PCB 焊盘定义是否一致；检查电源铜皮、退耦位置和晶振回路。
- 保存原理图 PDF、PCB 正反面、BOM、Gerber、ERC/DRC 结果和实物空板照片；当前目录还没有完整生产资料归档。

### 分阶段焊接与上电

1. 先焊 LDO 和电源电容，不焊 MCU。DP100 设 5 V 且限流 20–30 mA，确认 3.3 V 稳定且无异常发热。
2. 焊 MCU、每组退耦、VCAP、BOOT0、NRST、SWD。低限流上电，先读 3.3 V 和电流，再用 ST-Link 读取芯片 ID。
3. 烧录 HSI 低频 LED/串口最小程序，证明供电、复位、SWD 和 MCU 焊接正常。
4. 焊 8 MHz HSE 和负载电容，切换到 168 MHz；用 MCO 输出或定时器波形验证系统时钟。
5. 焊按键、LED、排针；做 GPIO walking-one、UART loopback、ADC/VREF 和复位/BOOT0 测试。

验收标准：静态电阻、电流、3.3 V 纹波、芯片 ID、SWD 下载、HSI/HSE、168 MHz、两颗 LED、按键、UART 和至少一组 ADC 全部留下可复现记录。

### 项目收口

- 单独建立 GitHub 仓库，目录包含 `hardware/`、`firmware/bringup/`、`docs/bringup-log.md`、`docs/images/` 和生产文件。
- 保留 PCB v1 问题清单和 v2 修改建议。即使 v1 有飞线，只要定位过程完整，反而比“直接点亮”更适合面试。
- 项目名称使用“STM32F407VET6 最小系统板设计与 Bring-up”，不要只写“画了一块 PCB”。

## 建议节奏

| 阶段 | 时间预算 | 输出 |
|---|---:|---|
| A | 3–5 天 | 项目 1 当前 UI/控制功能烧录回归、提交、`v1.0` |
| B | 2–3 天 | RAM/栈测量、协议测试、CI、24 h 稳定性与 OTA 故障注入 |
| C | 1 天 | F407 焊接前审查、BOM/生产资料归档、Bring-up checklist |
| D | 2–4 天 | 分阶段焊接、限流上电、SWD/时钟/GPIO/UART/ADC 验证 |
| E | 1–2 天 | 两个仓库 README、演示视频、简历条目和面试追问稿 |

如果时间被压缩，优先完成 A、C、D。一个完成度高、证据充分的系统项目加一块真正 Bring-up 的自制板，价值高于继续铺新功能。

## 简历表达草案

以下文本要等对应验收完成后再使用。

### STM32F103 + FreeRTOS + ESP32 智能环境终端

- 设计 STM32F103 自定义 Bootloader，将 64 KB Flash 划分为 8 KB Bootloader、54 KB Application 与 2 KB 配置区，实现向量表重定位及 ESP32 经 Bluetooth/Web 暂存、UART 分块下发的远程升级链路。
- 设计带 CRC-32、序号 ACK、超时重试和镜像向量表预检的通信协议；完成真实硬件 OTA，并通过错误 CRC、重复分块和链路中断测试验证异常恢复。
- 基于 FreeRTOS 划分通信、控制、采集/UI、状态灯和监控任务，使用 Queue、EventGroup、StreamBuffer、IWDG 及栈/heap 水位监控，在 20 KB SRAM 约束下完成多传感器、双屏与执行器集成。
- 使用逻辑分析仪从 WS2812B DIN/DOUT 建立证据链，定位 SPI 编码未被首颗灯珠接收的问题，并在 64 MHz 下校准 GPIO bit-bang 时序，实现 BH1750 自动调光和 Web/TFT 手动控制同步。

### STM32F407VET6 最小系统板设计与 Bring-up

- 使用嘉立创 EDA 完成 STM32F407VET6 LQFP100 两层最小系统板原理图与 PCB，设计 5 V→3.3 V 电源、HSE、复位、BOOT、SWD、VCAP、按键/LED 和全 GPIO 引出。
- 采用分阶段焊接与限流上电完成板级 Bring-up，依次验证电源完整性、芯片 ID、SWD 下载、HSI/HSE、168 MHz 系统时钟、GPIO、UART 与 ADC，并记录示波器波形和 v1 问题清单。
