# Build Notes — 2026-08-09

> 三端固件均能通过 PlatformIO 构建，且 Bluetooth SPP→ESP32→STM32 的 `VERSION` 往返已通过实机验证。完整 OTA 镜像传输仍未验收。

## 三端编译结果

| 固件 | 工具链 | RAM | Flash | 产物 |
|------|--------|-----|-------|------|
| Bootloader | `pio run -e bluepill` | 11.0% (2252B) | 10.9% (7128B) | `.pio/build/bluepill/firmware.bin` |
| Application | `pio run -e app` | 85.0% (17408B) | 23.8% (15588B) | `.pio/build/app/firmware.bin` |
| ESP32 Bridge | `pio run -d esp32-comm-bridge` | 19.7% (64448B) | 69.0% (1266293B / 1835008B) | `esp32-comm-bridge/.pio/build/esp32dev/firmware.bin` |

- Bootloader 产物 ~6KB，满足 **8KB 硬约束**
- Application 仍在 54KB 应用区内；RAM 余量约 3KB，后续增加传感器任务时需持续关注
- ESP32 开 Bluedroid 后固件 ~1.5MB，factory 分区必须 ≥ 1.5MB

## 硬件 Bring-up（2026-08-09）

- ESP32 CH340 串口为 `COM4`，STM32 通过 ST-Link SWD 烧录；两端写后校验均已通过。
- 链路接线：`GPIO17 → PA10`、`GPIO16 ← PA9`、GND 直连；STM32 使用 USART1，双方暂定 9600 baud。
- Windows Bluetooth Classic SPP 建立后，`COM6` 可发送 `STATUS` 与 `VERSION`；连续 10 次 `VERSION` 均返回 `FW Version: 0`。

## Application 编译要点

原来 `application/` 从未编译过（缺 FreeRTOS 内核、HAL、时钟配置），本次补全：

1. **Vendor FreeRTOS**：从 `framework-stm32cubef1/Middlewares/Third_Party/FreeRTOS/Source` 复制到 `application/lib/FreeRTOS/`（内核 + `GCC/ARM_CM3` 移植 + `heap_4`），根 `platformio.ini` 加 `[env:app]`
2. **64MHz 时钟**：实物板已确认 8MHz HSE；`main.c` 与 bootloader 均使用 HSE→PLL×8。USART1 当前运行在 9600 baud，用于先验证整条硬件链路
3. **FreeRTOS 必踩的坑**：
   - `configSUPPORT_STATIC_ALLOCATION=1` 时必须实现 `vApplicationGetIdleTaskMemory` / `vApplicationGetTimerTaskMemory`
   - 启动文件向量表用 `SVC_Handler`/`PendSV_Handler`，FreeRTOS 移植用 `vPortSVCHandler`/`xPortPendSVHandler` → 加薄包装函数
   - `SysTick_Handler` 里要调 `xPortSysTickHandler()`
4. **heap 8KB→12KB**：5 个动态任务栈合计 ~10KB，8KB heap 在调度器启动时直接触发 malloc-failed hook 挂死
5. **链接脚本**：`._user_heap_stack` 保留区从 8K 降到 1K（`heap_4` 用自己的静态数组，8K 保留区是纯浪费，会把 RAM 顶爆）

## ESP32 编译要点（ESP-IDF 6.0.1）

### 1. API 迁移（IDF 5→6 变了）

| 旧 API | IDF 6 |
|--------|-------|
| `esp_spp_init(ESP_SPP_MODE_CB)` | `esp_spp_enhanced_init(&cfg)`，`cfg.mode = ESP_SPP_MODE_CB` |
| `esp_bt_dev_set_device_name()` | `esp_bt_gap_set_device_name()`（`esp_gap_bt_api.h`） |
| `esp_wifi_is_connected()` | `esp_wifi_sta_get_ap_info() == ESP_OK` |

### 2. 蓝牙组件必须用 sdkconfig 开

`-D CONFIG_BT_SPP_ENABLED=1` 这类编译宏对 ESP-IDF **无效**（组件选择靠 Kconfig）。必须加 `sdkconfig.defaults`：

```
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_SPP_ENABLED=y
CONFIG_BT_CONTROLLER_ENABLED=y
CONFIG_BT_BLE_ENABLED=n
CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y  # Classic-only SPP；释放 BLE RAM
```

### 3. Flash 是 2MB 不是 4MB

本机这块 ESP32 实际 **2MB**（esp32dev 板默认 4MB）。配置：
- `platformio.ini`：`board_upload.flash_size = 2MB`（注意是 `upload` 不是 `build`）
- `sdkconfig.defaults`：`CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y`
- `partitions.csv`：factory `0x1C0000`（1.75MB）+ storage(SPIFFS) `0x30000`（192KB，存 ≤54KB 的 STM32 固件够用）

### 4. C/C++ 混编 flag 冲突 → protocol.cpp

PlatformIO 的 ESP-IDF 主组件里 `.c` 和 `.cpp` 并存时，会把 C 专用 flag（`-Wno-old-style-declaration`）和 C++ 专用 flag（`-fuse-cxa-atexit`）互相泄漏，双双报错。解法：把共享协议源码复制为 `src/protocol.cpp`（按 C++ 编译），并让 `shared/protocol.c` 兼容 C++：
- `proto_parser_feed` 的 `FRAME_STATE_CRC3` case 体加大括号（否则 C++ 报 `jump to case label`）
- `shared/protocol.c` 与 `esp32-comm-bridge/src/protocol.cpp` **字节一致**，改动时保持同步

### 5. ESP-IDF 忽略 build_src_filter

PlatformIO 的 ESP-IDF 框架用 CMake 管理源文件（`src/CMakeLists.txt` 里 `FILE(GLOB_RECURSE ...)`），`build_src_filter` 无效。加源文件就放进 `src/` 目录。

### 6. 国内网络需要代理

PlatformIO 官方包镜像（contabostorage / github release）在国内直连会被 SSL 掐断，导致装工具链卡住无限重试。编译前设代理：

```powershell
$env:HTTPS_PROXY='http://127.0.0.1:7897'   # 本机代理端口
pio run -d esp32-comm-bridge
```

## 蓝牙协议（实现现状）

服务名 `STM32-OTA-Bridge`。命令：`STATUS` / `VERSION` / `OTA <url>` / `FW <ver>,<size>,<crc32>` / `SEND` / `WIFI <ssid>,<pass>` / `RESET`。

蓝牙推固件的正确流程（`FW` + 二进制 + `SEND`）：
1. PC 发 `FW <version>,<size>,<crc32hex>` —— 声明精确大小、版本和标准 IEEE CRC-32，ESP32 清空旧暂存区
2. 发恰好 `<size>` 字节的二进制固件数据（SPP 逐块推）
3. 等 ESP32 返回 `FW: staged ...`；这说明文件长度完整，随后发 `SEND`
4. ESP32 复算 SPIFFS 文件 CRC，向应用发 `CMD_OTA_AVAILABLE`，等待应用以 `CMD_OTA_READY` 确认配置页已写好，再开始 Bootloader OTA

> 注意：SPP 回调里必须显式携带字节长度，固件 bin 含 NUL 字节，`strlen` 会截断。

> 当前已实测文本命令的 `STATUS` / `VERSION`。完整 OTA 已具备可执行脚本 `tools/bridge_ota.py`，仍需在刷写本轮三端固件后做一次实机验收。

## 下一步

- [x] 实机烧录 Bootloader / Application / ESP32，并验证 Bluetooth SPP 到 STM32 的双向命令链路
- [ ] 用一份已构建的 `app/firmware.bin` 验证端到端 OTA（暂存、触发重启、分块、CRC、回跳）
- [ ] 按 `project-framework.md` 的 Phase 0-7 上传感器驱动层（简历项目目标）
