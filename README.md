# STM32 Blue Pill OTA Bootloader System

基于 STM32F103C8T6 (Blue Pill) + FreeRTOS + ESP32 协处理器的远程固件升级系统，支持蓝牙和 WiFi OTA。

## 硬件需求

- **STM32F103C8T6** Blue Pill 开发板 (64KB Flash, 20KB SRAM)
- **ESP32** 开发板 (作为 BT/WiFi 通信桥)
- USB-TTL 串口模块 (用于 PC 调试)
- ST-Link/V2 烧录器 (或兼容的调试器)
- 杜邦线若干

## 接线

```
STM32 Blue Pill          ESP32
─────────────────        ────────────
PA2 (TX)  ─────────────► GPIO16 (RX)
PA3 (RX)  ◄───────────── GPIO17 (TX)
GND       ─────────────── GND

STM32 Blue Pill          ST-Link
─────────────────        ────────────
SWDIO     ────────────── SWDIO
SWCLK     ────────────── SWCLK
GND       ────────────── GND
3.3V      ────────────── 3.3V (可选, Blue Pill 可自供电)
```

## 快速开始

### 0. 初始化工具链

```bash
# 克隆项目
cd ~/Projects/bluepill-ota

# 安装 Python 测试工具依赖
pip install pyserial

# 安装 ESP32 工具链 (选择一种)
# 方式 A: PlatformIO (推荐)
#   安装 VSCode + PlatformIO 扩展
# 方式 B: ESP-IDF
#   https://docs.espressif.com/projects/esp-idf/en/latest/
```

### 1. 编译和烧录 Bootloader

1. 在 STM32CubeIDE 中创建新项目，选择 STM32F103C8Tx
2. 将 `bootloader/Core/` 中的源文件复制到项目
3. 将 `shared/` 目录添加到 include path
4. 使用 `bootloader/STM32F103C8TX_BOOT.ld` 替换默认链接脚本
5. 编译 → 使用 ST-Link 烧录到 `0x08000000`

### 2. 编译和烧录 Application

1. 在 STM32CubeIDE 中创建另一个项目（或使用多配置）
2. 将 `application/Core/` 中的源文件复制到项目
3. 添加 FreeRTOS 中间件（CMSIS-RTOS v2 或原生 FreeRTOS）
4. 将 `application/FreeRTOSConfig.h` 替换默认配置
5. 使用 `application/STM32F103C8TX_APP.ld` 替换链接脚本
6. 编译 → 烧录到 `0x08002000`

### 3. 编译和烧录 ESP32

```bash
cd esp32-comm-bridge

# PlatformIO
pio run -t upload

# 或 ESP-IDF
idf.py build flash
```

### 4. 测试 OTA

```bash
# 查询 STM32 状态
python tools/ota_sender.py /dev/tty.usbserial-XXXX --status

# 发送固件
python tools/ota_sender.py /dev/tty.usbserial-XXXX fw_v2.bin --version 2
```

## 蓝牙命令

连接 ESP32 的蓝牙 SPP 服务（设备名：`STM32-OTA-Bridge`）：

| 命令 | 说明 |
|------|------|
| `OTA <url>` | 从URL下载固件并更新STM32 |
| `VERSION` | 查询STM32当前固件版本 |
| `STATUS` | 查看ESP32桥接状态 |
| `RESET` | 软件复位ESP32 |

## 项目结构

参见 [docs/architecture.md](docs/architecture.md) 了解完整架构设计。

## 技术栈

- **STM32**: HAL 库, FreeRTOS, 自定义 Bootloader
- **ESP32**: ESP-IDF, Bluetooth Classic SPP, WiFi HTTP Client
- **协议**: 自定义帧协议 (CRC-32 校验, 1KB 页传输)
- **工具链**: STM32CubeIDE, PlatformIO/ESP-IDF, Python

## 关键约束

- STM32F103 单 Flash Bank → Flash 编程代码在 RAM 中执行 (`.ramfunc`)
- Flash 页大小: 1KB → OTA 以 1KB 块传输
- Bootloader 固定 8KB → 不能超出
- Application 起始于 `0x08002000` → 必须设置 `SCB->VTOR`

## License

MIT
