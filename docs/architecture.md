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
│       └──────────────┴───────────────────┘              │
│                         │ UART 460800                    │
│                    SPIFFS (/fw.bin)                      │
└─────────────────────────┬────────────────────────────────┘
                          │
                          │ UART (PA2/PA3)
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
│  │  - User application logic                        │   │
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

1. **Firmware staging** — ESP32 downloads firmware via WiFi or receives via Bluetooth, stores in SPIFFS
2. **Trigger** — ESP32 sends `CMD_OTA_AVAILABLE` to STM32 application
3. **Reboot** — STM32 app writes `BOOT_MODE_OTA` to config, calls `NVIC_SystemReset()`
4. **Bootloader entry** — Bootloader reads config, sees OTA request
5. **Handshake** — ESP32 sends `CMD_OTA_BEGIN` with firmware metadata
6. **Transfer** — ESP32 sends 1KB chunks via `CMD_OTA_CHUNK`, bootloader erases+programs flash
7. **Verify** — ESP32 sends `CMD_OTA_END`, bootloader computes CRC-32 over full image
8. **Commit** — CRC match: write config, jump to new application

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
Simple length-prefixed frames with CRC-32. Chunks are exactly 1KB (one flash page) for direct mapping: `addr = APP_BASE + seq * 1024`.

## FreeRTOS Task Architecture

| Task | Priority | Stack | Role |
|------|----------|-------|------|
| vCommTask | 3 | 768w | UART frame RX/TX, protocol parsing |
| vControlTask | 2 | 512w | OTA trigger, version reporting |
| vAppTask | 1 | 768w | User application logic |
| vLedTask | 1 | 256w | Status LED heartbeat |
| vMonitorTask | 1 | 256w | System health, stack/heap monitoring |

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
│   ├── Core/Inc/             # app_tasks, uart_comm, cmd_handler
│   ├── Core/Src/main.c       # FreeRTOS tasks
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
