# 智能环境监测与联动控制系统

> STM32 Blue Pill + FreeRTOS + ESP32 | 自定义 Bootloader + OTA | 多传感器 + Web仪表盘 + MQTT上云

基于 STM32F103C8T6 (Blue Pill) + FreeRTOS + ESP32 协处理器的**智能环境监测与联动控制系统**，支持蓝牙/WiFi OTA 固件升级，Web 实时仪表盘，MQTT 上云，以及传感器→决策→执行器闭环联动控制。

## 项目定位

嵌入式 MCU / RTOS 方向简历项目。覆盖 ARM Cortex-M3 裸机 Bootloader、FreeRTOS 多任务实时系统、7 种外设协议、多传感器驱动、ESP32 双模无线网关、以及 Web/蓝牙/Python 多层控制接口。

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
- **FreeRTOS**: 8 任务架构（Sensor/Event/Control/Display/Comm/App/Monitor/Led），任务通知/队列/事件组/互斥锁/流缓冲区
- **外设协议全覆盖**:
  - I2C×2 双总线（6 设备，Mutex 保护）
  - UART（双串口：ESP32 协议 + 调试日志）
  - 1-Wire（DHT11 bit-banging）
  - Timer Encoder Mode（旋转编码器硬件正交解码）
  - Timer Input Capture（HC-SR04 超声波测距）
  - PWM 50Hz（SG90 舵机角度控制）
  - ADC（MQ-2 烟雾浓度采集）
  - GPIO 外部中断（PIR 人体检测 + 对射红外门窗检测）

### ESP32 (C++, ESP-IDF)
- 蓝牙 Classic SPP 服务器（手机直连控制）
- WiFi HTTP Client（远程 OTA 固件下载）
- WebSocket Server（Web 仪表盘实时数据推送）
- HTTP REST API（传感器查询 + 控制指令）
- MQTT Client（阿里云 IoT / EMQX 数据上云）
- SPIFFS 固件缓存 + Web 页面托管

### 控制接口
- **Web 仪表盘**: 纯 HTML/CSS/JS SPA，响应式，支持手机浏览器
- **手机蓝牙**: Serial Bluetooth Terminal 文本命令
- **Python 桌面**: `ota_sender.py` 固件上传 + `control_panel.py` 控制面板

## 传感器清单

| 模块 | 协议 | 功能 | I2C 地址 |
|------|------|------|----------|
| AHT20 | I2C1 | 温湿度 | 0x38 |
| BMP280 | I2C2 | 气压 | 0x76 |
| BH1750 | I2C1 | 光照度 | 0x23 |
| MPU6050 | I2C2 | 6轴姿态 | 0x68 |
| VL53L0X | I2C2 | 激光测距 | 0x29 |
| SSD1306 OLED | I2C1 | 数据显示 | 0x3C |
| DHT11 | 1-Wire | 温湿度(冗余) | - |
| MQ-2 | ADC | 烟雾浓度 | - |
| HC-SR501 | GPIO INT | 人体红外 | - |
| 对射式红外 | GPIO INT | 门窗检测 | - |
| HC-SR04 | Timer IC | 超声波测距 | - |
| 旋转编码器 | Timer Enc | 菜单旋钮 | - |

## 执行器清单

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

PB6/PB7  (I2C1 SCL/SDA) ─── AHT20 + BH1750 + SSD1306
PB10/PB11 (I2C2 SCL/SDA) ─── BMP280 + MPU6050 + VL53L0X
PB12/PB13 (GPIO OUT) ─────── 继电器1/2
PB1      (EXTI) ──────────── HC-SR501 PIR
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

# Web 仪表盘
浏览器打开 http://<ESP32_IP>
```

## 蓝牙命令

连接 ESP32 蓝牙 SPP 服务 `STM32-OTA-Bridge`（手机用 "Serial Bluetooth Terminal"）：

| 命令 | 说明 |
|------|------|
| `STATUS` | 桥状态：BT/WiFi 连接、固件暂存情况 |
| `VERSION` | 查询 STM32 当前固件版本 |
| `OTA <url>` | 从 URL 下载固件并触发 OTA（版本从文件名 `fw_v<N>.bin` 解析） |
| `FW <ver>,<crc32>` | 开始蓝牙推送固件（重置接收状态，记录版本+CRC） |
| `SEND` | 把已暂存固件传输到 STM32（蓝牙推送完成后执行） |
| `WIFI <ssid>,<pass>` | 配置 WiFi 并重连（存 NVS，重启后仍生效） |
| `RESET` | 软件复位 ESP32 |

> 传感器查询/控制类命令（TEMP/RELAY1/SERVO/AUTO 等）属于 docs 规划的 Phase 扩展，当前固件未实现。

## 目录结构

```
bluepill-ota/
├── shared/                  # 共享协议 (CRC-32/帧解析/配置)
├── bootloader/              # 8KB 自定义 Bootloader
├── application/             # STM32 FreeRTOS 应用
├── esp32-comm-bridge/       # ESP32 通信网关
├── web-dashboard/           # Web 仪表盘
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

## 硬件链路验证（2026-08-09）

- ESP32（CH340，COM4）和 STM32（ST-Link SWD）均已完成烧录并通过写后校验。
- 物理 UART：`ESP32 GPIO17 → PA10`、`ESP32 GPIO16 ← PA9`、两板 GND 直连；使用 USART1，9600 baud。
- Windows 已通过 Bluetooth Classic SPP 连接 `STM32-OTA-Bridge`；`STATUS` 正常，`VERSION` 返回 STM32 的 `FW Version: 0`。
- 连续发送 10 次 `VERSION`，10/10 成功。此结论覆盖蓝牙→ESP32→UART→STM32 应用→回包，不等同于完整固件 OTA 已验证。

## 关键约束

- STM32F103 单 Flash Bank → Flash 编程代码在 RAM 中执行 (`.ramfunc`)
- Flash 页大小: 1KB → OTA 以 1KB 块传输
- Bootloader 固定 8KB → 不能超出
- Application 起始于 `0x08002000` → 必须设置 `SCB->VTOR`

## License

MIT
