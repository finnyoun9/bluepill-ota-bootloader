# Architecture: STM32 Blue Pill OTA Bootloader System

## Overview

```
┌──────────────────────┐          ┌──────────────────────┐
│     Phone / PC       │          │    OTA Server        │
│  (Bluetooth SPP)     │          │  (HTTP firmware)     │
└──────────┬───────────┘          └──────────┬───────────┘
           │ Bluetooth                        │ WiFi
           ▼                                  ▼
┌──────────────────────────────────────────────────────────┐
│                      ESP32                               │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐  │
│  │ BT SPP   │  │ WiFi HTTP│  │ OTA Orchestrator     │  │
│  │ Server   │  │ Client   │  │ (download/stage/xfer)│  │
│  └────┬─────┘  └────┬─────┘  └──────────┬───────────┘  │
│  SoftAP + Web OTA: STM32-OTA-Bridge / http://192.168.4.1 │
│       └──────────────┴───────────────────┘              │
│                         │ UART 115200                    │
│                    SPIFFS (/fw.bin)                      │
└─────────────────────────┬────────────────────────────────┘
                          │
                          │ USART1 (PA9/PA10)
                          │
┌─────────────────────────┴────────────────────────────────┐
│                   STM32F103C8T6                           │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │ Bootloader (0x08000000, 8KB)                     │   │
│  │  - OTA state machine                             │   │
│  │  - Flash erase/program (RAM-resident .ramfunc)   │   │
│  │  - CRC-32 verification                           │   │
│  │  - Application jump logic                        │   │
│  └──────────────────────────────────────────────────┘   │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │ Application (0x08002000, 54KB)                   │   │
│  │  - FreeRTOS (5 tasks)                            │   │
│  │  - UART protocol handler                         │   │
│  │  - OTA trigger (write config + NVIC_SystemReset) │   │
│  │  - Local sensor acquisition + OLED menu          │   │
│  └──────────────────────────────────────────────────┘   │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │ Config (0x0800F800, 2KB)                         │   │
│  │  - BootConfig_t (magic, mode, version, CRC)      │   │
│  └──────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────┘
```

## Flash Memory Map

| Region | Address | Size | Pages | Content |
|--------|---------|------|-------|---------|
| Bootloader | 0x08000000 | 8KB | 0-7 | OTA state machine, flash ops |
| Application | 0x08002000 | 54KB | 8-61 | FreeRTOS + user code |
| Config | 0x0800F800 | 2KB | 62-63 | Boot/OTA state |

## OTA Update Flow

1. **Firmware staging** — ESP32 downloads firmware via WiFi, or receives acknowledged Base64 `DATA` blocks via Bluetooth, then verifies and stores the image in SPIFFS
2. **Trigger** — ESP32 sends `CMD_OTA_AVAILABLE` to STM32 application
3. **Ready** — STM32 app writes `BOOT_MODE_OTA` to config, returns `CMD_OTA_READY`
4. **Reboot** — Application calls `NVIC_SystemReset()`; bootloader detects the flag and opens a 2 s OTA window
5. **Handshake** — ESP32 sends `CMD_OTA_BEGIN` with firmware metadata
6. **Transfer** — ESP32 sends 1KB chunks via `CMD_OTA_CHUNK`, bootloader erases+programs flash
7. **Verify** — ESP32 sends `CMD_OTA_END`, bootloader computes standard CRC-32 over full image
8. **Commit** — CRC match: write config, jump to new application

> 2026-08-09 已完成实机闭环：15,956 字节镜像通过 Bluetooth SPP 暂存，STM32 Bootloader 完成擦写与 CRC 校验，回跳后的 Application 返回 `FW Version: 1`。

The phone path uses the same steps after staging: `POST /api/upload?version=<N>` streams a binary image into SPIFFS, validates CRC and Cortex-M application vectors, then `POST /api/start` launches the shared OTA worker. `GET /api/status` exposes progress. The iPhone hardware test upgraded the application to version 2 without a PC sender.

## Key Design Decisions

### 1. Bootloader Does All Flash Work
The application never touches flash except to write the OTA request flag. This keeps the app simple and avoids the single-bank flash constraint during normal operation.

### 2. `.ramfunc` for Flash Programming
STM32F103 has a single flash bank — code executing from flash stalls during erase/program. Flash programming functions are placed in RAM via `__attribute__((section(".ramfunc")))` so the CPU stays alive.

### 3. Bootloader Entry Points (3 ways)
- Config flag `BOOT_MODE_OTA` set by application
- BOOT0 pin (PB0) pulled low — jumper/debug button
- 200ms passive UART window on every boot

### 4. Protocol Design
Simple length-prefixed frames with standard IEEE CRC-32. Chunk data is at most 1KB (one flash page) for direct mapping: `addr = APP_BASE + seq * 1024`; an OTA chunk payload is 1028 bytes because it also carries a 4-byte sequence number.

Bluetooth SPP staging uses printable `FW` / `DATA` / `VERIFY` commands. Each Base64 `DATA` block includes its decoded offset and receives an ACK carrying the next expected offset, so a lost response can be retried without appending the block twice. This outer staging protocol is separate from the binary UART frame protocol between ESP32 and STM32.

Web and Bluetooth transfers share a FreeRTOS mutex. The Web upload handler uses a fixed 1KB buffer and writes directly to SPIFFS, so the 54KB application image is never duplicated in ESP32 RAM.

## FreeRTOS Task Architecture

| Task | Priority | Stack | Role |
|------|----------|-------|------|
| vCommTask | 3 | 768w | UART frame RX/TX, protocol parsing |
| vControlTask | 2 | 512w | OTA trigger, version reporting |
| vAppTask | 1 | 768w | Sensor scheduling, EC11 navigation, PIR state, OLED UI |
| vLedTask | 1 | 256w | Status LED heartbeat |
| vMonitorTask | 1 | 256w | System health, stack/heap monitoring |

## Local Environment Terminal

The 2026-08-10 hardware checkpoint adds a local user interface without changing the OTA protocol path:

| Device | Interface | Current wiring | Role |
|--------|-----------|----------------|------|
| SSD1306 | I2C1, 0x3C | PB6/PB7 | Five-page 128×64 menu |
| BH1750 | I2C1, 0x23 | PB6/PB7 | Periodic lux measurement |
| AHT20 | I2C1, 0x38 | PB6/PB7 | Temperature and humidity with CRC-8 |
| BMP280 | I2C1, 0x76/0x77 | PB6/PB7 | Calibrated fixed-point pressure |
| EC11 A/B | GPIO EXTI | PA6/PA7 | Full Gray-code x1 navigation |
| Confirm button | GPIO input | PA1 | Enter/back action |
| HC-SR501 | GPIO input | PB0 | Motion state after 30 s warm-up |
| Relay 1/2 | GPIO output | PB12/PB13 | Humidifier (relay 1) and light (relay 2), active-low |

All I2C devices currently share the 100kHz I2C1 bus. `vAppTask` starts AHT20 and BMP280 conversions together, reads them after 90 ms, refreshes the environment data every 2 s, polls BH1750 every 200 ms, and only redraws the active OLED page. Sensor values stay in integer/fixed-point form because STM32F103 has no hardware FPU.

## Directory Structure

```
bluepill-ota/
├── shared/                   # Shared between all MCUs
│   ├── protocol.h/c          # Frame format, CRC-32, parser
│   └── ota_config.h/c        # Flash config read/write
├── bootloader/               # STM32CubeIDE bootloader project
│   ├── Core/Inc/bootloader.h
│   ├── Core/Src/main.c       # Boot state machine
│   └── STM32F103C8TX_BOOT.ld # 0x08000000, 8KB
├── application/              # STM32CubeIDE application project
│   ├── Core/Inc/             # Tasks, UART, I2C/sensor/input/display APIs
│   ├── Core/Src/             # FreeRTOS tasks and local terminal drivers
│   ├── FreeRTOSConfig.h
│   └── STM32F103C8TX_APP.ld  # 0x08002000, 54KB
├── esp32-comm-bridge/        # ESP-IDF project
│   ├── platformio.ini
│   └── src/main.cpp          # BT+WiFi+UART+Orchestrator
├── tools/
│   └── ota_sender.py         # PC-side firmware upload tool
└── docs/
    └── architecture.md
```
