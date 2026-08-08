# Build Notes — 2026-08-08

> 三端固件编译打通记录。核心结论：三个目标（Bootloader / Application / ESP32 Bridge）都通过 PlatformIO 编译，代码层面 OTA 链路闭环。**尚未接硬件实测。**

## 三端编译结果

| 固件 | 工具链 | RAM | Flash | 产物 |
|------|--------|-----|-------|------|
| Bootloader | `pio run -e bluepill` | 11.0% (2252B) | 8.8% (5756B) | `.pio/build/bluepill/firmware.bin` |
| Application | `pio run -e app` | 74.8% (15320B) | 23.8% (15592B) | `.pio/build/app/firmware.bin` |
| ESP32 Bridge | `pio run -d esp32-comm-bridge` | 21.8% (71464B) | 82.8% (1.52MB / 1.75MB) | `esp32-comm-bridge/.pio/build/esp32dev/firmware.bin` |

- Bootloader 产物 ~6KB，满足 **8KB 硬约束**
- Application 15.6KB，远小于 54KB 应用区
- ESP32 开 Bluedroid 后固件 ~1.5MB，factory 分区必须 ≥ 1.5MB

## Application 编译要点

原来 `application/` 从未编译过（缺 FreeRTOS 内核、HAL、时钟配置），本次补全：

1. **Vendor FreeRTOS**：从 `framework-stm32cubef1/Middlewares/Third_Party/FreeRTOS/Source` 复制到 `application/lib/FreeRTOS/`（内核 + `GCC/ARM_CM3` 移植 + `heap_4`），根 `platformio.ini` 加 `[env:app]`
2. **72MHz 时钟**：`main.c` 补 `SystemClock_Config()`（HSE 8MHz → PLL×9）。没有它 460800 波特率串口和 FreeRTOS tick 都不准
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
CONFIG_BTDM_CTRL_MODE_BTDM=y      # IDF 6 的符号，不是老的 BT_BTDM_CTRL_MODE
CONFIG_BTDM_CTRL_BLE_MAX_CONN=0
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

服务名 `STM32-OTA-Bridge`。命令：`STATUS` / `VERSION` / `OTA <url>` / `FW <ver>,<crc32>` / `SEND` / `WIFI <ssid>,<pass>` / `RESET`。

蓝牙推固件的正确流程（`FW` + 二进制 + `SEND`）：
1. 手机发 `FW <version>,<crc32hex>` —— 记录版本/CRC，重置接收
2. 发二进制固件数据（SPP 逐块推）
3. 发 `SEND` —— 触发 `transfer_to_stm32()` 走完整 OTA 流程

> 注意：SPP 回调里必须显式携带字节长度，固件 bin 含 NUL 字节，`strlen` 会截断。

## 下一步

- [ ] 接硬件实测：ST-Link 烧 bootloader/app，`pio run -d esp32-comm-bridge -t upload` 烧 ESP32
- [ ] `python tools/ota_sender.py COM3 fw_v2.bin --version 2` 验证端到端 OTA
- [ ] 按 `project-framework.md` 的 Phase 0-7 上传感器驱动层（简历项目目标）
