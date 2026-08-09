# 智能环境监测与联动控制系统

> STM32 Blue Pill + FreeRTOS + ESP32 | 自定义 Bootloader + OTA | 多传感器 + Web仪表盘 + MQTT上云

基于 STM32F103C8T6 (Blue Pill) + FreeRTOS + ESP32 协处理器的**智能环境监测与联动控制系统**。当前已完成自定义 Bootloader、蓝牙 OTA、手机 Web OTA，以及 STM32 本地环境终端；传感器→决策→执行器、远程实时仪表盘与 MQTT 属于后续扩展目标。

> 想快速回顾每次提交到底改了什么，可看双语 [CHANGELOG.md](CHANGELOG.md)。它会区分“已经实机验证”和“仍是设计/待验证”的内容。

## 项目定位

嵌入式 MCU / RTOS 方向简历项目。当前核心覆盖 ARM Cortex-M3 裸机 Bootloader、FreeRTOS 多任务、ESP32 双模无线网关、自定义 UART 协议、Web/蓝牙/Python 三条 OTA 入口，以及 I2C 多设备采集和旋钮式本地 OLED 菜单。

> 下方系统图、传感器和执行器清单包含项目目标架构；是否已经实现以“硬件 OTA 闭环验证”和 [CHANGELOG.md](CHANGELOG.md) 为准。

## 系统架构

```
传感器层 (STM32)          网关层 (ESP32)           控制层
┌─────────────────┐    ┌─────────────────┐    ┌──────────────┐
│ I2C×2 (6设备)   │    │ WebSocket 推送  │    │ Web 仪表盘   │
│ 1-Wire (DHT11)  │───►│ HTTP REST API   │───►│ 手机浏览器   │
│ ADC (MQ-2)      │    │ MQTT 上云       │    │ 蓝牙终端     │
│ Timer Encoder   │    │ 蓝牙 SPP        │    │ Python 桌面  │
│ PWM (舵机)      │    │ OTA 固件管理    │    └──────────────┘
│ GPIO 中断       │    └─────────────────┘
│ 继电器/蜂鸣器   │
└─────────────────┘
     ▲
     │ UART 自定义协议 (CRC-32)
     │
┌─────────────────┐
│  Bootloader 8KB │ ← OTA 固件升级
└─────────────────┘
```

## 技术栈

### STM32 (C, FreeRTOS)
- **Bootloader**: 8KB 裸机，Flash 分区管理，`.ramfunc` RAM 执行，CRC-32 校验，OTA 状态机
- **FreeRTOS**: 当前 5 任务架构（Comm/Control/App/Monitor/Led）；`AppTask` 已整合本地传感器采集、PIR 状态和 OLED 菜单
- **当前外设**:
  - I2C1 100kHz 多设备总线：SSD1306、BH1750、AHT20、BMP280
  - GPIO/EXTI：EC11 四状态解码、独立确认按键、HC-SR501 输入
- **后续规划协议**:
  - I2C2（MPU6050、VL53L0X）
  - UART（双串口：ESP32 协议 + 调试日志）
  - 1-Wire（DHT11 bit-banging）
  - Timer Encoder Mode（旋转编码器硬件正交解码）
  - Timer Input Capture（HC-SR04 超声波测距）
  - PWM 50Hz（SG90 舵机角度控制）
  - ADC（MQ-2 烟雾浓度采集）
  - GPIO 外部中断（PIR 人体检测 + 对射红外门窗检测）

### ESP32 (C++, ESP-IDF)
- 蓝牙 Classic SPP 服务器（手机直连控制）
- WiFi SoftAP + 手机 Web OTA（已实机验证）
- WiFi HTTP Client（远程 OTA 固件下载）
- SPIFFS 固件缓存 + Application 向量表/CRC 校验
- WebSocket、传感器 REST API、MQTT（后续规划）

### 控制接口
- **手机 Web OTA**: ESP32 内置响应式页面，iPhone 可直接上传 `.bin`
- **Web 仪表盘**: 纯 HTML/CSS/JS SPA（后续规划）
- **蓝牙终端**: Windows/Android Classic SPP 文本命令（iPhone 不支持 SPP）
- **Python 桌面**: `ota_sender.py` 固件上传 + `control_panel.py` 控制面板

## 传感器进度

| 模块 | 当前接口 | 功能 | 地址/引脚 | 状态 |
|------|----------|------|-----------|------|
| AHT20 + BMP280 组合板 | I2C1 | 温度、湿度、气压 | 0x38 + 0x76/0x77 | 实机通过 |
| BH1750 | I2C1 | 光照度 | 0x23 | 实机通过 |
| SSD1306 OLED | I2C1 | 本地菜单与数据显示 | 0x3C | 实机通过 |
| HC-SR501 | GPIO | 人体红外 | PB0 | 实机通过 |
| EC11 旋转编码器 | GPIO EXTI + GPIO | 菜单上下/确认 | PA6/PA7 + PA1 | 实机通过 |
| MPU6050 | I2C2（规划） | 6轴姿态 | 0x68 | 待接入 |
| VL53L0X | I2C2（规划） | 激光测距 | 0x29 | 待接入 |
| DHT11 | 1-Wire（规划） | 温湿度冗余 | - | 待接入 |
| MQ-2 | ADC（规划） | 烟雾浓度 | - | 待接入 |
| HC-SR04 | Timer IC（规划） | 超声波测距 | - | 待接入 |

## 规划执行器清单

| 模块 | 控制方式 | 联动场景 |
|------|---------|---------|
| 2路继电器 | GPIO OUT | 风扇/加湿器/灯光 |
| 超声波雾化片 | GPIO OUT | 湿度<40%自动加湿 |
| SG90 舵机 | PWM 50Hz | 百叶窗/阀门角度 |
| 有源蜂鸣器 | GPIO OUT | 烟雾/高温告警 |

## Flash 内存布局

| 区域 | 地址 | 大小 | 说明 |
|------|------|------|------|
| Bootloader | 0x08000000 | 8KB | OTA 状态机 + Flash 编程 |
| Application | 0x08002000 | 54KB | FreeRTOS + 传感器 + 控制 |
| Config | 0x0800F800 | 2KB | OTA 状态 + 固件版本 |

## 硬件接线

完整引脚分配和面包板接线图见 [docs/project-framework.md](docs/project-framework.md)

```
STM32 Blue Pill          ESP32              传感器/执行器
─────────────────        ────────────        ────────────
PA9 (TX)  ────────────► GPIO16 (RX)
PA10 (RX) ◄──────────── GPIO17 (TX)
GND       ─────────────── GND

PB6/PB7  (I2C1 SCL/SDA) ─── AHT20+BMP280 + BH1750 + SSD1306
PA6/PA7  (GPIO EXTI) ─────── EC11 A/B
PA1      (GPIO IN) ───────── EC11 确认按键
PB0      (GPIO IN) ───────── HC-SR501 PIR
PB10/PB11 (I2C2，规划) ───── MPU6050 + VL53L0X
PB12/PB13 (GPIO OUT) ─────── 继电器1/2
PA0      (TIM2_CH1) ──────── SG90 舵机
PA5      (ADC) ───────────── MQ-2 烟雾 (经1k/2k分压)
```

## 快速开始

### 工具链

```bash
pip install pyserial        # Python 工具
pio --version               # PlatformIO（编译 STM32 两端 + ESP32）
```

### 构建

```bash
pio run -e bluepill                # Bootloader → .pio/build/bluepill/firmware.bin
pio run -e app                     # Application → .pio/build/app/firmware.bin
pio run -d esp32-comm-bridge       # ESP32 桥 → esp32-comm-bridge/.pio/build/esp32dev/firmware.bin
```

### 烧录

1. **Bootloader**: ST-Link 执行 `pio run -e bluepill -t upload`，写入 `0x08000000`
2. **Application**: ST-Link 执行 `pio run -e app -t upload`，写入 `0x08002000`
3. **ESP32**: 手动进入下载模式后执行 `pio run -d esp32-comm-bridge -t upload --upload-port COM4`

### 测试

```bash
# Windows 蓝牙 SPP 出站端口（本机当前为 COM6）
pio device monitor -p COM6 -b 115200

# 在 monitor 中输入 STATUS 或 VERSION 并按 Enter

# 端到端蓝牙 OTA（先刷写本次 Bootloader、Application 和 ESP32 固件）
C:\Users\yyfxy\AppData\Local\Programs\Python\Python311\python.exe `
  tools\bridge_ota.py COM6 .pio\build\app\firmware.bin --version 1

# 手机 Web OTA：连接 ESP32 热点后访问
# SSID: STM32-OTA-Bridge / Password: stm32ota
浏览器打开 http://192.168.4.1
```

## 手机 Web OTA

1. 手机连接 ESP32 热点 `STM32-OTA-Bridge`，密码 `stm32ota`
2. Safari/浏览器打开 `http://192.168.4.1`
3. 选择 STM32 Application 的 `firmware.bin`，填写大于 0 的目标版本
4. 点击“开始 OTA 升级”，等待页面显示“升级成功”

页面通过 `POST /api/upload?version=<N>` 上传固件，ESP32 检查 54KB 大小上限、CRC-32、Cortex-M 初始栈和 Application Reset Vector；校验通过后由 `POST /api/start` 启动 UART OTA。`GET /api/status` 返回上传和写入进度。

> Web OTA 只接受链接到 `0x08002000` 的 STM32 Application 镜像。Bootloader 或其他目标生成的 `.bin` 会被向量表检查拒绝。

## 蓝牙命令

连接 ESP32 蓝牙 SPP 服务 `STM32-OTA-Bridge`（手机用 "Serial Bluetooth Terminal"）：

| 命令 | 说明 |
|------|------|
| `STATUS` | 桥状态：BT/WiFi 连接、固件暂存情况 |
| `VERSION` | 查询 STM32 当前固件版本 |
| `OTA <url>` | 从 URL 下载固件并触发 OTA（版本从文件名 `fw_v<N>.bin` 解析） |
| `FW <ver>,<size>,<crc32>` | 开始蓝牙推送固件；声明精确长度、版本和标准 CRC-32 |
| `DATA <offset>,<base64>` | 写入一块 Base64 固件数据；ESP32 用下一偏移量 ACK（通常由脚本自动发送） |
| `VERIFY` | 校验暂存文件长度和 CRC-32（通常由脚本自动发送） |
| `SEND` | 在桥返回 `FW: staged` 后，把已校验的暂存固件传输到 STM32 |
| `WIFI <ssid>,<pass>` | 配置 WiFi 并重连（存 NVS，重启后仍生效） |
| `RESET` | 软件复位 ESP32 |

> 传感器查询/控制类命令（TEMP/RELAY1/SERVO/AUTO 等）属于 docs 规划的 Phase 扩展，当前固件未实现。

> `tools/bridge_ota.py` 会自动计算 `<size>` 和 CRC，把固件编码为带偏移量 ACK 的 Base64 分块，执行 `VERIFY` 后再发送 `SEND`。不要在普通串口终端里手工粘贴二进制 `.bin`。

## 目录结构

```
bluepill-ota/
├── shared/                  # 共享协议 (CRC-32/帧解析/配置)
├── bootloader/              # 8KB 自定义 Bootloader
├── application/             # STM32 FreeRTOS 应用
├── esp32-comm-bridge/       # ESP32 通信网关
├── tools/                   # Python PC 工具
└── docs/                    # 架构 + 接线 + API 文档
```

完整说明见 [docs/project-framework.md](docs/project-framework.md)

## 本地验证记录（2026-08-07）

- Bootloader 已通过 PlatformIO + `ststm32` 平台编译验证：RAM 11.0% (2252B)，Flash 8.8% (5756B)，`firmware.bin` 约 6KB（满足 bootloader 8KB 硬约束）
- 修复：`shared/protocol.h` 中 `BootConfig_t` 大小断言 56 → 48（实际 12×uint32=48B）；`FLASH_BASE`/`FLASH_PAGE_SIZE` 加 `#ifndef` 保护避免与 STM32 HAL 重定义
- PC 端工具：`python tools/ota_sender.py COM3 fw.bin --version 2`（Windows 串口用 COM 格式）
- 注意：ESP32 端编译需 PlatformIO 能访问官方包镜像（国内网络建议配置镜像源）

## 本地验证记录（2026-08-08）

**三端固件全部编译通过**（PlatformIO + ststm32 / espressif32）：

| 固件 | RAM | Flash | 产物 |
|------|-----|-------|------|
| Bootloader | 11.0% (2252B) | 8.8% (5756B) | `.pio/build/bluepill/firmware.bin` |
| Application | 74.8% (15320B) | 23.8% (15592B) | `.pio/build/app/firmware.bin` |
| ESP32 Bridge | 21.8% (71464B) | 82.8% (1.52MB) | `esp32-comm-bridge/.pio/build/esp32dev/firmware.bin` |

关键结论（细节见 [docs/build-notes.md](docs/build-notes.md)）：
- **ESP-IDF 6 API 迁移**：`esp_spp_init`→`esp_spp_enhanced_init(&cfg)`；`esp_bt_dev_set_device_name`→`esp_bt_gap_set_device_name`；`esp_wifi_is_connected`→`esp_wifi_sta_get_ap_info()`
- **ESP32 实际是 2MB Flash**（esp32dev 默认 4MB）：分区表 factory 1.75MB + storage(SPIFFS) 192KB；Bluedroid 固件 ~1.5MB，factory 必须这么大
- **蓝牙组件用 sdkconfig.defaults 开启**（`-D CONFIG_BT_*` 编译宏对 ESP-IDF 无效）
- **共享协议跨平台**：`shared/protocol.c` 已 C/C++ 兼容；ESP32 端镜像为 `src/protocol.cpp` 编译
- **国内网络编译 ESP32 需代理**：`$env:HTTPS_PROXY='http://127.0.0.1:7897'; pio run -d esp32-comm-bridge`
- Application 使用 8MHz HSE→PLL×8 的 64MHz 时钟；当前 STM32↔ESP32 链路统一为 9600 baud

## 硬件 OTA 闭环验证（2026-08-09）

- ESP32（CH340，COM4）和 STM32（ST-Link SWD）均已完成烧录并通过写后校验。
- 物理 UART：`ESP32 GPIO17 → PA10`、`ESP32 GPIO16 ← PA9`、两板 GND 直连；使用 USART1，9600 baud。
- Windows 已通过 Bluetooth Classic SPP 的 COM6 连接 `STM32-OTA-Bridge`；基础链路连续发送 10 次 `VERSION`，10/10 成功。
- 使用 `tools/bridge_ota.py` 暂存并发送 15,956 字节 Application 镜像，CRC-32 为 `0x3B274D7E`；ESP32 返回 `STATUS: OTA complete!`。
- OTA 后再次发送 `VERSION`，STM32 返回 `FW Version: 1`。这次已经覆盖 PC→蓝牙 SPP→ESP32 SPIFFS→STM32 Bootloader→Flash/CRC→Application 回跳的完整闭环。
- 排查中发现两份 CRC 查表各有 4 个错误常量；`123456789` 经典向量恰好没有触发。现在额外用 `0x00..0xFF` 全字节向量（期望 `0x29058C73`）做回归，避免同类问题被单一测试向量漏掉。
- iPhone 已连接 ESP32 SoftAP 并打开内置 Web OTA 页面；手机上传 15,956 字节 Application 镜像、目标版本设为 2，升级后通过 COM6 查询返回 `FW Version: 2`。该路径不需要 PC、ST-Link 或 ESP32 USB 参与固件发送。

## 本地环境终端验证（2026-08-10）

- I2C1 `PB6/PB7 @ 100kHz` 同时挂载 SSD1306、BH1750 和 AHT20+BMP280 组合板，四个地址均已在实机稳定工作。
- OLED 菜单支持旋转选择、按键确认和返回；页面包括环境、光照、人体感应、系统状态与项目信息。
- AHT20/BMP280 已显示温度、相对湿度和气压；湿度使用定点数并显示为 `61.0% RH`，未引入软件浮点。
- HC-SR501 支持 30 秒预热状态和 HIGH/LOW 检测；BH1750 光照值可周期刷新。
- Application 构建占用：RAM 17,676 B（86.3%），Flash 25,200 B（38.5%）；后续扩展需优先关注 RAM 余量。

## 关键约束

- STM32F103 单 Flash Bank → Flash 编程代码在 RAM 中执行 (`.ramfunc`)
- Flash 页大小: 1KB → OTA 以 1KB 块传输
- Bootloader 固定 8KB → 不能超出
- Application 起始于 `0x08002000` → 必须设置 `SCB->VTOR`

### 已知限制：OTA 传输中途掉电无回退

Bootloader 收到第一个 chunk 就会擦除 Application 区第 0 页（含中断向量表），所以 OTA 传输中途掉电会让旧固件立即失效：下次上电 `app_is_valid()` 判定应用无效，设备停在 Bootloader 的 maintenance 模式，需要重新完成一次 OTA 或用 ST-Link 重刷才能恢复。这是 64KB 单 Bank Flash、没有预留 A/B 分区空间的直接结果——生产级方案通常会先写暂存区、整体校验后再原子切换，但在 8KB Bootloader + 54KB Application 的预算下没有空间做这件事。建议 OTA 过程中保证电源稳定，避免中途拔线断电。

## License

MIT
