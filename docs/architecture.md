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
│  Sensor poll/cache → GET /api/sensors → Web dashboard   │
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

### 3. Bootloader Entry Points
- Config flag `BOOT_MODE_OTA` set by application
- 200ms passive UART window on every boot

PB0 is reserved for the HC-SR501 PIR input and is not sampled by the
bootloader. Recovery still uses the Blue Pill's dedicated hardware BOOT0
jumper to enter the STM32 ROM bootloader, or ST-Link/SWD to reflash either
image.

### 4. Protocol Design
Simple length-prefixed frames with standard IEEE CRC-32. Chunk data is at most 1KB (one flash page) for direct mapping: `addr = APP_BASE + seq * 1024`; an OTA chunk payload is 1028 bytes because it also carries a 4-byte sequence number.

Bluetooth SPP staging uses printable `FW` / `DATA` / `VERIFY` commands. Each Base64 `DATA` block includes its decoded offset and receives an ACK carrying the next expected offset, so a lost response can be retried without appending the block twice. This outer staging protocol is separate from the binary UART frame protocol between ESP32 and STM32.

Web and Bluetooth transfers share a FreeRTOS mutex. The Web upload handler uses a fixed 1KB buffer and writes directly to SPIFFS, so the 54KB application image is never duplicated in ESP32 RAM.

### 5. Realtime sensor snapshot path

`vAppTask` publishes an 18-byte `SensorSnapshot_t` every 200ms into a shared fixed-size snapshot. It contains fixed-point environment readings, lux, PIR state, relay/buzzer/auto flags, and LED brightness. `vCommTask` copies it inside a short critical section when it receives `CMD_GET_SENSOR_SNAPSHOT (0x32)`, then replies with `CMD_SENSOR_SNAPSHOT_RSP (0x87)` using the normal CRC-protected UART frame.

The ESP32 `sensor_poll` task queries once per second and writes the reply into a Mutex-protected cache. `GET /api/sensors` formats that cache as JSON without touching UART, so slow browsers cannot block the MCU link. Sensor polling, Bluetooth request/response commands, and OTA share one UART transaction mutex. During OTA, polling skips cycles; data older than 5 seconds is reported as offline.

The browser polls `/api/sensors` once per second. WebSocket and historical storage remain separate future layers. See [web-realtime-dashboard.md](web-realtime-dashboard.md) for the payload, API fields, and verification steps.

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
| GMT020-02 ST7789 | SPI2, 16MHz | PB13/PB15 + PB12/PB14/PA8 | Mirrored 240×320 portrait UI |
| 15× WS2812B | GPIO bit-bang @ 64MHz | PB5 | Inverse BH1750-controlled white lighting |
| BH1750 | I2C1, 0x23 | PB6/PB7 | Periodic lux measurement |
| AHT20 | I2C1, 0x38 | PB6/PB7 | Temperature and humidity with CRC-8 |
| BMP280 | I2C1, 0x76/0x77 | PB6/PB7 | Calibrated fixed-point pressure |
| EC11 A/B | GPIO EXTI | PA6/PA7 | Full Gray-code x1 navigation |
| Confirm button | GPIO input | PA1 | Enter / light-power toggle |
| Back button | GPIO input | PA4 | Active-low return to TFT menu |
| HC-SR501 | GPIO input | PB0 | Motion state after 30 s warm-up |
| Relay 1/2 | GPIO output | PA2/PA3 | Relay 1 unused; relay 2 / NO2 switches light-strip VCC, active-low |
| Active buzzer | GPIO output | PB1 | Alarm output, active-low |

All I2C devices currently share the 100kHz I2C1 bus. `vAppTask` starts AHT20 and BMP280 conversions together, reads them after 90 ms, refreshes the environment data every 2 s, and polls BH1750 every 200 ms. The same 5..1000 lux range drives a smoothed inverse 160..1 brightness curve for the WS2812B strip, with a 16-level slew step and a 2-level deadband. Because the 15-LED GPIO frame requires a roughly 0.5ms critical section, the application transmits only when the smoothed brightness actually changes. The SSD1306 is now an always-on four-line status display, while the ST7789 owns a five-item menu and detail/control pages. TFT drawing is direct and change-cached: selection cards, values, and the brightness bar update without a framebuffer. Sensor values stay in integer/fixed-point form because STM32F103 has no hardware FPU.

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
